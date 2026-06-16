#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "event_config.hpp"
#include "event_state_tracker.hpp"
#include "inference_request_queue.hpp"
#include "metadata_writer.hpp"
#include "vlm_inference_manager.hpp"

namespace vlm_event_monitor
{

class JpegRingBufferStage;

// One detection event delivered to subscribers (e.g. SseBroadcaster) when
// an event-check cycle yields ≥1 YES verdict. The runner builds one
// TriggeredEvent per YES verdict per cycle and dispatches them in order.
//
// `frames` contains raw JPEG bytes — the same JPEGs synchronized with the
// RGB frames that produced this verdict. Count is mode-dependent: 3 for
// Performance, 4 for Accuracy. The broadcaster base64-encodes them before
// sending over SSE.
struct TriggeredEvent
{
    uint32_t event_id = 0;
    std::string event_description;
    std::string vlm_description; // raw VLM response text (per design §6.3)
    std::chrono::system_clock::time_point timestamp{};
    std::vector<std::vector<uint8_t>> frames;

    // Optional debug bundle. Populated by the runner only when
    // debug_metadata_save_enabled is true; empty otherwise. The bundle
    // mirrors the on-disk /var/volatile/vlm-event-metadata/<cycle>/
    // contents so the frontend's per-event "Download debug bundle" button
    // can package an identical zip client-side. When absent, the SSE
    // payload omits the `debug` field and the frontend hides the button.
    struct DebugBundle
    {
        std::string cycle_id;    // "<timestamp>-<seq>"
        nlohmann::json metadata; // same shape as metadata.json
        std::vector<std::vector<uint8_t>> frames_336_jpeg;
    };
    std::optional<DebugBundle> debug_bundle;
};

// Runtime knobs forwarded from the YAML config (vlm_app_config::EventCheckConfig).
struct EventCheckRunnerConfig
{
    // Mode at startup. Switchable at runtime via EventCheckRunner::set_mode.
    // Each mode determines its own frame-window size and sample positions
    // (see kPerformance/kAccuracy constants in event_check_runner.cpp).
    EventCheckMode mode = EventCheckMode::Performance;

    // Per-event yes/no token budget (used by both Performance and Accuracy
    // for each yes/no inference).
    uint32_t max_generated_tokens_per_event = 4;

    // Performance-mode prompt lead-in. Forwarded verbatim to PromptBuilder,
    // which appends the numbered event list. Unused in Accuracy mode.
    // Sourced from YAML (event_check.performance.lead_prompt).
    std::string performance_lead_prompt;

    // Accuracy-mode tunables. Sourced from YAML (event_check.accuracy.*).
    std::string accuracy_lead_prompt = "describe this video sequence in one sentence";
    uint32_t accuracy_max_tokens = 30;

    // Debug-only override (vlm_app_config::DebugPromptOverride). When
    // `debug_override_enabled` is true the runner sends `debug_override_prompt`
    // verbatim with `debug_override_max_tokens`, bypassing both modes.
    // Sourced from YAML (event_check.debug_prompt_override.*).
    bool debug_override_enabled = false;
    std::string debug_override_prompt;
    uint32_t debug_override_max_tokens = 100;

    // Debug-only metadata dump. Sourced from YAML
    // (event_check.debug_metadata_save.*).
    bool debug_metadata_save_enabled = false;
    uint32_t debug_metadata_save_keep_last = 10;

    // Snapshot of VLM model config used to populate metadata.json. Stage
    // 2 sources these from the same YAML block that drives the manager.
    std::string vlm_hef_path;
    uint32_t vlm_default_max_generated_tokens = 256;
};

// Owns the event-check loop. Receives RGB frames pushed from
// Nv12ToRgbStage's callback, accumulates them into mode-specific windows,
// samples mode-specific frame indices, and submits inference work to the
// InferenceRequestQueue.
//
// Mode switching: set_mode() can be called from any thread; it swaps the
// active mode under lock and discards any partial accumulator. A cycle
// already in flight finishes with the old mode; the next push uses the
// new one.
class EventCheckRunner
{
  public:
    // Result-broadcast hook invoked once per YES verdict on a cycle. Wired
    // in vlm_pipeline.cpp to SseBroadcaster::push_new_event so the frontend
    // sees triggered events in real time.
    using ResultBroadcastFn = std::function<void(const TriggeredEvent &event)>;

    EventCheckRunner(std::shared_ptr<InferenceRequestQueue> queue, EventCheckRunnerConfig config,
                     std::vector<UserEvent> initial_events, std::weak_ptr<JpegRingBufferStage> jpeg_ring = {},
                     std::shared_ptr<EventStateTracker> event_state_tracker = nullptr);
    ~EventCheckRunner();

    EventCheckRunner(const EventCheckRunner &) = delete;
    EventCheckRunner &operator=(const EventCheckRunner &) = delete;

    void start();
    void stop();

    // Push one RGB frame produced by Nv12ToRgbStage. When the mode-specific
    // window is full, the sampled subset is submitted to the inference
    // queue and the accumulator resets. While a cycle is in flight,
    // incoming frames are dropped.
    void push_rgb_frame(std::vector<uint8_t> rgb);

    // Replace the active event list at runtime. Stage 3 wires this to
    // /api/events PUT (via EventStore::replace_all → apply callback).
    void set_events(std::vector<UserEvent> events);

    // Switch event-check mode. Discards any partial accumulator and starts
    // collecting frames for the new mode's window on the next push. Safe
    // to call from any thread.
    void set_mode(EventCheckMode mode);

    EventCheckMode mode() const;

    // Snapshot of the active event list under lock. Used by the REST layer
    // for /api/monitoring/status.
    std::vector<UserEvent> events() const;

    // Whether start() has been called and stop() hasn't. Used by
    // /api/monitoring/status.
    bool is_running() const
    {
        return m_running.load();
    }

    // Subscribe to triggered-event notifications. Replaces any prior
    // callback. Pass nullptr to clear. Safe to call from any thread.
    void set_result_broadcaster(ResultBroadcastFn fn);

    // Wire the JPEG ring after construction. The pipeline builder can't
    // pass it in the constructor because the ring is created later, in
    // build_pipeline(). Safe to call from any thread; subsequent cycles
    // pick up the new ring.
    void set_jpeg_ring(std::weak_ptr<JpegRingBufferStage> ring);

  private:
    // Per-cycle state for Performance mode. Held on the heap so the queue's
    // on_complete callback keeps it alive after submit_performance_batch
    // returns. `frames` is empty unless the metadata writer is enabled.
    // `jpeg_frames` is captured at dispatch from JpegRingBufferStage at
    // exactly the same indices as the RGB samples; populated only when a
    // result broadcaster is registered.
    struct PerformanceCycle
    {
        std::vector<std::vector<uint8_t>> frames;      // empty when metadata save is off
        std::vector<std::vector<uint8_t>> jpeg_frames; // empty when no broadcaster registered
        std::vector<UserEvent> events;
        std::string prompt;
        uint32_t max_tokens = 0;
        std::chrono::steady_clock::time_point started_at{};
    };

    // Per-cycle state for Debug-override mode. Same lifecycle pattern as
    // PerformanceCycle.
    struct DebugOverrideCycle
    {
        std::vector<std::vector<uint8_t>> frames; // empty when metadata save is off
        std::string prompt;
        uint32_t max_tokens = 0;
        std::chrono::steady_clock::time_point started_at{};
    };

    // Per-cycle state for Accuracy mode. The N sampled frames are sent ONLY
    // with the first (description) inference; subsequent per-event yes/no
    // inferences are text-only follow-ups on the same VLM session, reusing
    // the visual context the description established.
    //
    // Held on the heap as a shared_ptr so the queue's on_complete callbacks
    // can keep it alive across multiple chained inferences.
    struct AccuracyCycle
    {
        std::vector<std::vector<uint8_t>> frames;
        std::vector<std::vector<uint8_t>> jpeg_frames; // empty when no broadcaster registered
        std::vector<UserEvent> events;                 // enabled events captured at cycle start

        // VLM session that owns the visual context for the entire cycle.
        // Created in start_accuracy_cycle, closed in on_accuracy_cycle_done
        // (or earlier on abort). 0 = not created.
        uint32_t session_id = 0;

        // Description result.
        bool description_ok = false;
        std::string description_error;
        InferenceResult description_result;

        // Per-event results (parallel arrays, len == events.size() when done).
        std::vector<bool> event_ok;
        std::vector<std::string> event_errors;
        std::vector<InferenceResult> event_results;
        std::vector<bool> event_verdicts;

        // 0 = description; 1..events.size() = events[index-1].
        size_t next_inference = 0;

        std::chrono::steady_clock::time_point cycle_start{};
    };

    void submit_performance_batch(std::vector<std::vector<uint8_t>> frames);
    void submit_debug_override_batch(std::vector<std::vector<uint8_t>> frames);
    void start_accuracy_cycle(std::vector<std::vector<uint8_t>> frames);
    void submit_next_accuracy_inference(std::shared_ptr<AccuracyCycle> cycle);
    void on_accuracy_cycle_done(std::shared_ptr<AccuracyCycle> cycle);

    void on_performance_inference_done(std::shared_ptr<PerformanceCycle> cycle,
                                       tl::expected<InferenceResult, std::string> result);
    void on_debug_override_done(std::shared_ptr<DebugOverrideCycle> cycle,
                                tl::expected<InferenceResult, std::string> result);

    // Metadata save helpers. Performance / accuracy versions ALSO build a
    // SSE-inline DebugBundle (336x336 JPEGs encoded in-memory + the metadata
    // JSON the on-disk file holds) which the broadcaster uses to populate
    // the optional `debug` SSE field. Return std::nullopt when
    // m_metadata_writer is null OR when the cycle has no frames.
    // Debug-override doesn't broadcast triggered events, so its helper
    // stays void.
    std::optional<TriggeredEvent::DebugBundle> save_performance_metadata(const PerformanceCycle &cycle,
                                                                         const InferenceResult &result,
                                                                         const std::vector<bool> &verdicts);
    std::optional<TriggeredEvent::DebugBundle> save_accuracy_metadata(const AccuracyCycle &cycle);
    void save_debug_override_metadata(const DebugOverrideCycle &cycle, const InferenceResult &result);

    std::shared_ptr<InferenceRequestQueue> m_queue;
    EventCheckRunnerConfig m_config;

    // mutable so const events() accessor can lock for a read.
    mutable std::mutex m_events_mutex;
    std::vector<UserEvent> m_events;

    // Protects m_pending_frames, m_mode, and m_dropped_frame_count.
    // mutable so const accessors (mode()) can lock for a read.
    mutable std::mutex m_buffer_mutex;
    // Bounded ring buffer of RGB frames waiting for the next event-check
    // cycle. Always sized at most window_size_for(m_mode); when a new
    // frame would overflow during an in-flight cycle the OLDEST frame is
    // evicted and m_dropped_frame_count is incremented. The counter is
    // captured & reported on the next dispatch and reset on set_mode/stop.
    std::deque<std::vector<uint8_t>> m_pending_frames;
    uint32_t m_dropped_frame_count = 0;
    EventCheckMode m_mode;

    std::atomic<bool> m_inference_in_flight{false};
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_batches_submitted{0};

    // Created lazily in the constructor only when metadata save is enabled
    // in config. nullptr otherwise — every save call short-circuits.
    std::shared_ptr<MetadataWriter> m_metadata_writer;

    // Weak handle to the JPEG ring. Used at cycle dispatch to
    // snapshot the JPEGs synchronized with the RGB samples for SSE delivery.
    // weak_ptr to avoid holding the ring alive past the pipeline's teardown.
    std::weak_ptr<JpegRingBufferStage> m_jpeg_ring;

    // Triggered-event notification hook. Wired to
    // SseBroadcaster::push_new_event in vlm_pipeline.cpp. nullptr → no
    // broadcasting; cycles still run, JPEGs are not snapshotted, frames are
    // discarded after inference.
    mutable std::mutex m_broadcaster_mutex;
    ResultBroadcastFn m_result_broadcaster;

    // Per-event cooldown tracker. Consulted inside
    // broadcast_yes_verdicts to suppress repeated YES verdicts for the
    // same event_id within the cooldown window.
    std::shared_ptr<EventStateTracker> m_event_state_tracker;

    // Helper: pull the JPEGs from m_jpeg_ring at the same indices the RGB
    // sampler used. Returns empty if no broadcaster is registered, the ring
    // is gone, or the ring doesn't contain enough frames for the window.
    std::vector<std::vector<uint8_t>> snapshot_jpegs_for_mode(EventCheckMode mode) const;

    // Helper: turn a finished cycle's events + verdicts into a sequence of
    // TriggeredEvent objects (one per YES) and dispatch each through the
    // broadcaster. JPEG frames are copied into each event from the cycle's
    // jpeg_frames snapshot. When `debug_bundle` is provided, every YES
    // event gets a copy of it for client-side download.
    void broadcast_yes_verdicts(const std::vector<UserEvent> &events, const std::vector<bool> &verdicts,
                                const std::vector<std::vector<uint8_t>> &jpeg_frames,
                                const std::string &vlm_description,
                                const std::optional<TriggeredEvent::DebugBundle> &debug_bundle);
};

} // namespace vlm_event_monitor
