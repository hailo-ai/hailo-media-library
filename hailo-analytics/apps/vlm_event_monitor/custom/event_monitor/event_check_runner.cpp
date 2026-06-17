#include "event_check_runner.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

#include "../pipeline/jpeg_ring_buffer_stage.hpp"
#include "prompt_builder.hpp"

namespace vlm_event_monitor
{

namespace
{
// Mode-specific window/sample layout. Assumes the medialib profile outputs
// VlmInput at 1 FPS. When that rate changes (e.g. 2 FPS) these need to
// scale accordingly.
constexpr size_t kPerformanceWindowFrames = 5;
constexpr size_t kAccuracyWindowFrames = 10;

const std::vector<size_t> &performance_sample_indices()
{
    // Frames 1, 3, 5 (1-indexed) within a 5-frame window.
    static const std::vector<size_t> indices{0, 2, 4};
    return indices;
}

const std::vector<size_t> &accuracy_sample_indices()
{
    // Frames 2, 4, 6, 8 (1-indexed) within a 10-frame window.
    static const std::vector<size_t> indices{1, 3, 5, 7};
    return indices;
}

size_t window_size_for(EventCheckMode mode)
{
    return mode == EventCheckMode::Accuracy ? kAccuracyWindowFrames : kPerformanceWindowFrames;
}

const std::vector<size_t> &sample_indices_for(EventCheckMode mode)
{
    return mode == EventCheckMode::Accuracy ? accuracy_sample_indices() : performance_sample_indices();
}

// Templated so it accepts both std::vector and std::deque of frames.
template <typename Container>
std::vector<std::vector<uint8_t>> sample_frames(Container &accumulated, const std::vector<size_t> &indices)
{
    std::vector<std::vector<uint8_t>> sampled;
    sampled.reserve(indices.size());
    for (size_t index : indices)
    {
        if (index < accumulated.size())
        {
            sampled.push_back(std::move(accumulated[index]));
        }
    }
    return sampled;
}

// Consume the next ASCII alpha run at/after `scan`, lowercased. Mirrors the
// helper used by the existing console diag tool's parse_yesno_to_json so
// behaviour is consistent across both code paths (the parser works on the
// model's output regardless of who issued the prompt).
std::string consume_alpha_token(const std::string &response, size_t &scan)
{
    while (scan < response.size() && !std::isalpha(static_cast<unsigned char>(response[scan])))
    {
        scan++;
    }
    std::string token;
    while (scan < response.size() && std::isalpha(static_cast<unsigned char>(response[scan])))
    {
        token += static_cast<char>(std::tolower(static_cast<unsigned char>(response[scan])));
        scan++;
    }
    return token;
}

// Parse "1. Yes\n2. No\n3. Yes\n…" into one verdict per slot. Returns a
// vector aligned with `expected_count`; unparseable slots default to false.
std::vector<bool> parse_yesno_response(const std::string &response, size_t expected_count)
{
    std::vector<bool> verdicts(expected_count, false);
    std::vector<bool> matched(expected_count, false);

    // Numbered scan: "<digit>. Yes|No"
    for (size_t scan = 0; scan < response.size(); scan++)
    {
        if (!std::isdigit(static_cast<unsigned char>(response[scan])))
        {
            continue;
        }
        size_t digit_start = scan;
        while (scan < response.size() && std::isdigit(static_cast<unsigned char>(response[scan])))
        {
            scan++;
        }
        if (scan >= response.size() || response[scan] != '.')
        {
            continue;
        }
        int event_id = std::stoi(response.substr(digit_start, scan - digit_start));
        scan++; // skip '.'
        while (scan < response.size() && std::isspace(static_cast<unsigned char>(response[scan])))
        {
            scan++;
        }
        if (scan >= response.size())
        {
            break;
        }
        std::string token = consume_alpha_token(response, scan);
        if (event_id < 1 || static_cast<size_t>(event_id) > expected_count)
        {
            continue;
        }
        size_t slot = static_cast<size_t>(event_id - 1);
        if (token == "yes")
        {
            verdicts[slot] = true;
            matched[slot] = true;
        }
        else if (token == "no")
        {
            verdicts[slot] = false;
            matched[slot] = true;
        }
    }

    // Bare-token fallback for the single-event case where the model drops "1. ".
    bool any_matched = false;
    for (size_t index = 0; index < expected_count; index++)
    {
        if (matched[index])
        {
            any_matched = true;
            break;
        }
    }
    if (!any_matched)
    {
        size_t slot_to_fill = 0;
        size_t scan = 0;
        while (scan < response.size() && slot_to_fill < expected_count)
        {
            std::string token = consume_alpha_token(response, scan);
            if (token.empty())
            {
                break;
            }
            if (token == "yes")
            {
                verdicts[slot_to_fill] = true;
                slot_to_fill++;
            }
            else if (token == "no")
            {
                verdicts[slot_to_fill] = false;
                slot_to_fill++;
            }
        }
    }

    return verdicts;
}

// Scan a single-question response (e.g. "Yes.", "no", "Yes, the person …")
// for the first yes/no alpha token. Returns true if "yes". Used by
// Accuracy mode where each event gets its own one-shot inference.
bool parse_single_yesno(const std::string &response)
{
    size_t scan = 0;
    while (scan < response.size())
    {
        std::string token = consume_alpha_token(response, scan);
        if (token.empty())
        {
            return false;
        }
        if (token == "yes")
        {
            return true;
        }
        if (token == "no")
        {
            return false;
        }
    }
    return false;
}

std::vector<UserEvent> filter_enabled(const std::vector<UserEvent> &events)
{
    std::vector<UserEvent> enabled;
    enabled.reserve(events.size());
    for (const auto &event : events)
    {
        if (event.enabled)
        {
            enabled.push_back(event);
        }
    }
    return enabled;
}
} // namespace

EventCheckRunner::EventCheckRunner(std::shared_ptr<InferenceRequestQueue> queue, EventCheckRunnerConfig config,
                                   std::vector<UserEvent> initial_events, std::weak_ptr<JpegRingBufferStage> jpeg_ring,
                                   std::shared_ptr<EventStateTracker> event_state_tracker)
    : m_queue(std::move(queue)), m_config(config), m_events(std::move(initial_events)), m_mode(config.mode),
      m_jpeg_ring(std::move(jpeg_ring)), m_event_state_tracker(std::move(event_state_tracker))
{
    if (m_config.debug_metadata_save_enabled)
    {
        m_metadata_writer = std::make_shared<MetadataWriter>("/var/volatile/vlm-event-metadata",
                                                             m_config.debug_metadata_save_keep_last);
    }
}

EventCheckRunner::~EventCheckRunner()
{
    stop();
}

void EventCheckRunner::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
    {
        return;
    }
    HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: started (mode={}, window={})", to_string(m_mode),
                             window_size_for(m_mode));
}

void EventCheckRunner::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        m_pending_frames.clear();
        m_dropped_frame_count = 0;
    }
    HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: stopped (batches_submitted={})", m_batches_submitted.load());
}

void EventCheckRunner::set_events(std::vector<UserEvent> events)
{
    std::lock_guard<std::mutex> lock(m_events_mutex);
    m_events = std::move(events);
    HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: events updated (count={})", m_events.size());
}

void EventCheckRunner::set_mode(EventCheckMode mode)
{
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    if (m_mode == mode)
    {
        return;
    }
    m_mode = mode;
    // Discard any partial accumulator — the new window may have a different
    // size, and stale frames would corrupt sampling. The drop counter is
    // reset alongside since drops accumulated for the previous mode aren't
    // meaningful for the new one.
    m_pending_frames.clear();
    m_dropped_frame_count = 0;
    HAILO_ANALYTICS_LOG_INFO("EventCheckRunner: mode -> {} (window={})", to_string(mode), window_size_for(mode));
}

EventCheckMode EventCheckRunner::mode() const
{
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    return m_mode;
}

std::vector<UserEvent> EventCheckRunner::events() const
{
    std::lock_guard<std::mutex> lock(m_events_mutex);
    return m_events;
}

void EventCheckRunner::set_result_broadcaster(ResultBroadcastFn fn)
{
    std::lock_guard<std::mutex> lock(m_broadcaster_mutex);
    m_result_broadcaster = std::move(fn);
}

void EventCheckRunner::set_jpeg_ring(std::weak_ptr<JpegRingBufferStage> ring)
{
    std::lock_guard<std::mutex> lock(m_broadcaster_mutex);
    m_jpeg_ring = std::move(ring);
}

std::vector<std::vector<uint8_t>> EventCheckRunner::snapshot_jpegs_for_mode(EventCheckMode mode_snapshot) const
{
    std::shared_ptr<JpegRingBufferStage> ring;
    {
        // Single critical section: skip if no broadcaster, otherwise also
        // grab the live ring ptr while we hold the lock. Keeps the JPEG-copy
        // cost off the production path until SSE has consumers.
        std::lock_guard<std::mutex> lock(m_broadcaster_mutex);
        if (!m_result_broadcaster)
        {
            return {};
        }
        ring = m_jpeg_ring.lock();
    }
    if (!ring)
    {
        return {};
    }
    auto all = ring->snapshot_all();
    const size_t window = window_size_for(mode_snapshot);
    if (all.size() < window)
    {
        // Ring hasn't filled yet; bail rather than send a partial sample.
        return {};
    }

    // Take the most-recent `window` JPEGs (oldest of those = front of the
    // window) and pick the same indices the RGB sampler used.
    std::vector<std::vector<uint8_t>> windowed(all.end() - static_cast<long>(window), all.end());
    std::vector<std::vector<uint8_t>> sampled;
    const auto &indices = sample_indices_for(mode_snapshot);
    sampled.reserve(indices.size());
    for (size_t index : indices)
    {
        if (index < windowed.size())
        {
            sampled.push_back(std::move(windowed[index]));
        }
    }
    return sampled;
}

void EventCheckRunner::broadcast_yes_verdicts(const std::vector<UserEvent> &events, const std::vector<bool> &verdicts,
                                              const std::vector<std::vector<uint8_t>> &jpeg_frames,
                                              const std::string &vlm_description,
                                              const std::optional<TriggeredEvent::DebugBundle> &debug_bundle)
{
    ResultBroadcastFn broadcaster_copy;
    {
        std::lock_guard<std::mutex> lock(m_broadcaster_mutex);
        broadcaster_copy = m_result_broadcaster;
    }
    if (!broadcaster_copy)
    {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    for (size_t index = 0; index < events.size() && index < verdicts.size(); index++)
    {
        if (!verdicts[index])
        {
            continue;
        }

        // Per-event cooldown. try_emit atomically stamps the
        // tracker on success; if the event_id is still inside its window,
        // suppress this YES without forwarding to the broadcaster.
        if (m_event_state_tracker && !m_event_state_tracker->try_emit(events[index].id))
        {
            HAILO_ANALYTICS_LOG_DEBUG("EventCheckRunner: suppressing event_id={} '{}' (within cooldown)",
                                      events[index].id, events[index].description);
            continue;
        }

        TriggeredEvent triggered;
        triggered.event_id = events[index].id;
        triggered.event_description = events[index].description;
        triggered.vlm_description = vlm_description;
        triggered.timestamp = now;
        triggered.frames = jpeg_frames; // copy — broadcaster may need to encode/queue

        // Each YES event gets its own copy of the debug bundle (cycle-wide
        // data — every YES from the same cycle has the same bundle).
        triggered.debug_bundle = debug_bundle;
        broadcaster_copy(triggered);
    }
}

void EventCheckRunner::push_rgb_frame(std::vector<uint8_t> rgb)
{
    if (!m_running.load())
    {
        return;
    }

    EventCheckMode mode_snapshot;
    std::vector<std::vector<uint8_t>> sampled;
    uint32_t drops_to_report = 0;
    {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        mode_snapshot = m_mode;
        const size_t window = window_size_for(mode_snapshot);

        // Continuous accumulation: keep the buffer bounded at `window`. While
        // a cycle is in flight, evict the OLDEST frame on overflow so the
        // next cycle starts with the most-recent window's worth of context.
        const bool in_flight = m_inference_in_flight.load();
        if (in_flight && m_pending_frames.size() >= window)
        {
            m_pending_frames.pop_front();
            m_dropped_frame_count++;
        }
        m_pending_frames.push_back(std::move(rgb));

        // If a cycle is still in flight, just continue accumulating. The
        // next cycle will dispatch as soon as inference completes and a
        // new frame arrives (or, if the buffer is already full at that
        // point, on the very next push).
        if (in_flight)
        {
            return;
        }

        // Inference is idle. Wait until we have a full window before
        // dispatching.
        if (m_pending_frames.size() < window)
        {
            return;
        }

        // Ready to dispatch. Capture & reset the drop counter so we can
        // surface a warning, sample the latest window via the mode's
        // indices, and claim the in-flight slot atomically with the
        // dispatch decision.
        sampled = sample_frames(m_pending_frames, sample_indices_for(mode_snapshot));
        m_pending_frames.clear();
        drops_to_report = m_dropped_frame_count;
        m_dropped_frame_count = 0;
        m_inference_in_flight.store(true);
    }

    if (sampled.empty())
    {
        m_inference_in_flight.store(false);
        return;
    }

    if (drops_to_report > 0)
    {
        /*
        std::cout << "──── EventCheckRunner: WARNING ────\n"
                  << "  dropped " << drops_to_report
                  << " frame(s) — inference is not keeping up with the configured\n"
                  << "  window; oldest frames evicted from the accumulator\n"
                  << "────────────────────────────────────\n"
                  << std::flush;
        */
        HAILO_ANALYTICS_LOG_WARN("EventCheckRunner: dropped {} frame(s) — slow inference catching up with window",
                                 drops_to_report);
    }

    // Debug override short-circuits both modes.
    if (m_config.debug_override_enabled)
    {
        submit_debug_override_batch(std::move(sampled));
        return;
    }

    if (mode_snapshot == EventCheckMode::Accuracy)
    {
        start_accuracy_cycle(std::move(sampled));
    }
    else
    {
        submit_performance_batch(std::move(sampled));
    }
}

// ─── Performance mode (single aggregated inference) ─────────────────────────

void EventCheckRunner::submit_performance_batch(std::vector<std::vector<uint8_t>> frames)
{
    auto cycle = std::make_shared<PerformanceCycle>();
    {
        std::lock_guard<std::mutex> lock(m_events_mutex);
        cycle->events = filter_enabled(m_events);
    }
    if (cycle->events.empty())
    {
        HAILO_ANALYTICS_LOG_DEBUG("EventCheckRunner: no enabled events; skipping batch");
        // Empty events list — release the in-flight slot so the next push
        // can dispatch as soon as one is available.
        m_inference_in_flight.store(false);
        return;
    }
    cycle->prompt = PromptBuilder::build_user_prompt(m_config.performance_lead_prompt, cycle->events);
    cycle->max_tokens = static_cast<uint32_t>(m_config.max_generated_tokens_per_event * cycle->events.size());
    cycle->started_at = std::chrono::steady_clock::now();

    // Stash a copy of the frames ONLY when metadata save is enabled — keeps
    // the production path zero-cost.
    if (m_metadata_writer)
    {
        cycle->frames = frames;
    }

    // Snapshot synchronized JPEGs at dispatch. The JPEG ring is a
    // sliding window; reading it here pins the exact JPEGs that pair with
    // the RGB samples we're about to send to inference. Empty when no
    // result broadcaster is registered.
    cycle->jpeg_frames = snapshot_jpegs_for_mode(EventCheckMode::Performance);

    InferenceRequest request;
    request.frames = std::move(frames);
    request.prompt = cycle->prompt;
    request.max_generated_tokens = cycle->max_tokens;
    request.use_video_mode = request.frames.size() > 1;

    InferenceJob job;
    job.priority = InferencePriority::EventCheck;
    job.work = [request](VlmInferenceManager &manager) { return manager.infer_oneshot(request); };
    job.on_complete = [this, cycle](tl::expected<InferenceResult, std::string> result) {
        m_inference_in_flight.store(false);
        on_performance_inference_done(cycle, std::move(result));
    };
    job.enqueued_at = std::chrono::steady_clock::now();

    // m_inference_in_flight is already set true by push_rgb_frame under the
    // buffer lock. We only need to reset it on submission failure.
    if (!m_queue->submit(std::move(job)))
    {
        // EventCheck submissions disabled (e.g. paused during chat).
        m_inference_in_flight.store(false);
        return;
    }
    m_batches_submitted.fetch_add(1, std::memory_order_relaxed);
}

void EventCheckRunner::on_performance_inference_done(std::shared_ptr<PerformanceCycle> cycle,
                                                     tl::expected<InferenceResult, std::string> result)
{
    if (!result)
    {
        HAILO_ANALYTICS_LOG_ERROR("EventCheckRunner: inference failed: {}", result.error());
        return;
    }

    const auto &inference_result = result.value();
    const auto verdicts = parse_yesno_response(inference_result.response, cycle->events.size());

    /*
    std::cout << "──── VLM event-check result (performance) ────\n"
              << "  raw response : " << inference_result.response << "\n"
              << "  ttft_ms      : " << static_cast<int>(inference_result.stats.ttft_ms) << "\n"
              << "  total_ms     : " << static_cast<int>(inference_result.stats.total_ms) << "\n"
              << "  tps          : " << inference_result.stats.tps << "\n"
              << "  ctx_tokens   : " << inference_result.stats.context_usage << " / "
              << inference_result.stats.context_capacity << "\n"
              << "  events (" << cycle->events.size() << "):\n";
    for (size_t index = 0; index < cycle->events.size(); index++)
    {
        std::cout << "    [" << cycle->events[index].id << "] " << (verdicts[index] ? "YES" : "no ") << " - "
                  << cycle->events[index].description << "\n";
    }
    std::cout << "────────────────────────────────────\n" << std::flush;
    */

    auto debug_bundle = save_performance_metadata(*cycle, inference_result, verdicts);

    // Dispatch one TriggeredEvent per YES verdict to the
    // SseBroadcaster (no-op when no broadcaster registered). Also each
    // YES carries the optional 336x336+metadata bundle for client download.
    broadcast_yes_verdicts(cycle->events, verdicts, cycle->jpeg_frames, inference_result.response, debug_bundle);
}

// ─── Accuracy mode (description + per-event chained inferences) ─────────────

void EventCheckRunner::start_accuracy_cycle(std::vector<std::vector<uint8_t>> frames)
{
    auto cycle = std::make_shared<AccuracyCycle>();
    cycle->frames = std::move(frames);
    {
        std::lock_guard<std::mutex> lock(m_events_mutex);
        cycle->events = filter_enabled(m_events);
    }
    if (cycle->events.empty())
    {
        HAILO_ANALYTICS_LOG_DEBUG("EventCheckRunner: no enabled events; skipping accuracy cycle");
        // Empty events list — release the in-flight slot.
        m_inference_in_flight.store(false);
        return;
    }
    cycle->cycle_start = std::chrono::steady_clock::now();
    // Snapshot synchronized JPEGs at dispatch for SSE delivery.
    // Empty when no broadcaster is registered.
    cycle->jpeg_frames = snapshot_jpegs_for_mode(EventCheckMode::Accuracy);

    // Create a single VLM session for the whole cycle. The description
    // inference will load the N frames into this session's context;
    // subsequent per-event inferences are text-only follow-ups that reuse
    // that visual context.
    auto session_result = m_queue->manager()->create_session();
    if (!session_result)
    {
        HAILO_ANALYTICS_LOG_ERROR("EventCheckRunner: create_session failed: {}", session_result.error());
        // The in-flight slot was claimed by push_rgb_frame; release it so
        // the next frame can dispatch.
        m_inference_in_flight.store(false);
        return;
    }
    cycle->session_id = session_result.value();

    // m_inference_in_flight is already set true by push_rgb_frame under the
    // buffer lock; held high for the entire chained-inference cycle.
    submit_next_accuracy_inference(std::move(cycle));
}

void EventCheckRunner::submit_next_accuracy_inference(std::shared_ptr<AccuracyCycle> cycle)
{
    if (cycle->next_inference > cycle->events.size())
    {
        on_accuracy_cycle_done(std::move(cycle));
        return;
    }

    InferenceRequest request;

    if (cycle->next_inference == 0)
    {
        // Description: send the N frames once, into the session's context.
        request.frames = cycle->frames;
        request.prompt = m_config.accuracy_lead_prompt;
        request.max_generated_tokens = m_config.accuracy_max_tokens;
        request.use_video_mode = request.frames.size() > 1;
    }
    else
    {
        // Per-event yes/no: TEXT-ONLY follow-up on the same session. The
        // visual context loaded by the description inference is reused —
        // sending frames again here would re-encode the visual tokens
        // unnecessarily.
        const auto &event = cycle->events[cycle->next_inference - 1];
        request.frames = {}; // intentionally empty — text-only follow-up

        // Avoid producing "??" if the user already wrote a trailing "?".
        std::string desc = event.description;
        while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.back())))
        {
            desc.pop_back();
        }
        const std::string suffix =
            (!desc.empty() && desc.back() == '?') ? " just answer with Yes or No" : "? just answer with Yes or No";
        request.prompt = desc + suffix;
        request.max_generated_tokens = m_config.max_generated_tokens_per_event;
        request.use_video_mode = false;
    }

    InferenceJob job;
    job.priority = InferencePriority::EventCheck;
    const uint32_t session_id = cycle->session_id;
    job.work = [request, session_id](VlmInferenceManager &manager) { return manager.infer(session_id, request); };
    job.on_complete = [this, cycle](tl::expected<InferenceResult, std::string> result) mutable {
        if (cycle->next_inference == 0)
        {
            cycle->description_ok = result.has_value();
            if (result)
            {
                cycle->description_result = std::move(result.value());
            }
            else
            {
                cycle->description_error = result.error();
                // Description failed — there is no valid visual context to
                // run the per-event follow-ups against. Finalize and close
                // the session.
                on_accuracy_cycle_done(cycle);
                return;
            }
        }
        else
        {
            cycle->event_ok.push_back(result.has_value());
            if (result)
            {
                cycle->event_errors.push_back("");
                cycle->event_results.push_back(std::move(result.value()));
            }
            else
            {
                cycle->event_errors.push_back(result.error());
                cycle->event_results.push_back(InferenceResult{});
            }
        }
        cycle->next_inference++;
        if (cycle->next_inference > cycle->events.size())
        {
            on_accuracy_cycle_done(cycle);
        }
        else
        {
            submit_next_accuracy_inference(cycle);
        }
    };
    job.enqueued_at = std::chrono::steady_clock::now();

    if (!m_queue->submit(std::move(job)))
    {
        // EventCheck submissions disabled mid-cycle (rare). Close session
        // and abort.
        if (cycle->session_id != 0)
        {
            m_queue->manager()->close_session(cycle->session_id);
            cycle->session_id = 0;
        }
        m_inference_in_flight.store(false);
        return;
    }
    if (cycle->next_inference == 0)
    {
        m_batches_submitted.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventCheckRunner::on_accuracy_cycle_done(std::shared_ptr<AccuracyCycle> cycle)
{
    // Parse each event's yes/no verdict.
    cycle->event_verdicts.clear();
    cycle->event_verdicts.reserve(cycle->event_results.size());
    for (size_t index = 0; index < cycle->event_results.size(); index++)
    {
        if (cycle->event_ok[index])
        {
            cycle->event_verdicts.push_back(parse_single_yesno(cycle->event_results[index].response));
        }
        else
        {
            cycle->event_verdicts.push_back(false);
        }
    }

    /*
    const auto cycle_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - cycle->cycle_start).count();
    size_t total_ctx_tokens = 0;
    if (cycle->description_ok)
    {
        total_ctx_tokens += cycle->description_result.stats.context_usage;
    }

    for (size_t index = 0; index < cycle->event_results.size(); index++)
    {
        if (cycle->event_ok[index])
        {
            total_ctx_tokens += cycle->event_results[index].stats.context_usage;
        }
    }

    std::cout << "──── VLM event-check result (accuracy) ────\n";
    std::cout << "  description prompt   : " << m_config.accuracy_lead_prompt << "\n";
    if (cycle->description_ok)
    {
        const auto &description = cycle->description_result;
        std::cout << "  description response : " << description.response << "\n"
                  << "  description ttft_ms  : " << static_cast<int>(description.stats.ttft_ms) << "\n"
                  << "  description total_ms : " << static_cast<int>(description.stats.total_ms) << "\n"
                  << "  description ctx_toks : " << description.stats.context_usage << " / "
                  << description.stats.context_capacity << "\n";
    }
    else
    {
        std::cout << "  description FAILED   : " << cycle->description_error << "\n";
    }

    std::cout << "  events (" << cycle->events.size() << "):\n";
    for (size_t index = 0; index < cycle->events.size(); index++)
    {
        const auto &event = cycle->events[index];
        std::cout << "    [" << event.id << "] ";
        if (index < cycle->event_ok.size() && cycle->event_ok[index])
        {
            std::cout << (cycle->event_verdicts[index] ? "YES" : "no ") << " - " << event.description
                      << "  (resp=\"" << cycle->event_results[index].response
                      << "\" ttft=" << static_cast<int>(cycle->event_results[index].stats.ttft_ms) << "ms"
                      << " total=" << static_cast<int>(cycle->event_results[index].stats.total_ms) << "ms"
                      << " ctx=" << cycle->event_results[index].stats.context_usage << ")\n";
        }
        else
        {
            std::string error = (index < cycle->event_errors.size()) ? cycle->event_errors[index] : "n/a";
            std::cout << "ERR - " << event.description << "  (" << error << ")\n";
        }
    }
    std::cout << "  cycle total_ms       : " << cycle_total_ms << "\n"
              << "  cycle total_ctx_toks : " << total_ctx_tokens << "\n"
              << "────────────────────────────────────\n"
              << std::flush;
    */

    // Release the session — frees its saved KV-cache context. Done after
    // the banner so any context_usage reads above are valid.
    if (cycle->session_id != 0)
    {
        auto close_result = m_queue->manager()->close_session(cycle->session_id);
        if (!close_result)
        {
            HAILO_ANALYTICS_LOG_WARN("EventCheckRunner: close_session({}) failed: {}", cycle->session_id,
                                     close_result.error());
        }
        cycle->session_id = 0;
    }

    auto debug_bundle = save_accuracy_metadata(*cycle);

    // Dispatch one TriggeredEvent per YES verdict. The "vlm
    // description" field is the description-inference response — it's the
    // closest thing this mode has to a free-form scene description.
    // Eeach YES carries the optional 336x336+metadata bundle.
    const std::string vlm_description = cycle->description_ok ? cycle->description_result.response : std::string{};
    broadcast_yes_verdicts(cycle->events, cycle->event_verdicts, cycle->jpeg_frames, vlm_description, debug_bundle);

    m_inference_in_flight.store(false);
}

// ─── Debug override (single inference, no parsing) ──────────────────────────

void EventCheckRunner::submit_debug_override_batch(std::vector<std::vector<uint8_t>> frames)
{
    auto cycle = std::make_shared<DebugOverrideCycle>();
    cycle->prompt = m_config.debug_override_prompt;
    cycle->max_tokens = m_config.debug_override_max_tokens;
    cycle->started_at = std::chrono::steady_clock::now();
    if (m_metadata_writer)
    {
        cycle->frames = frames;
    }

    InferenceRequest request;
    request.frames = std::move(frames);
    // Verbatim user prompt — no system role, no event list, no parsing.
    request.prompt = cycle->prompt;
    request.max_generated_tokens = cycle->max_tokens;
    request.use_video_mode = request.frames.size() > 1;

    InferenceJob job;
    job.priority = InferencePriority::EventCheck;
    job.work = [request](VlmInferenceManager &manager) { return manager.infer_oneshot(request); };
    job.on_complete = [this, cycle](tl::expected<InferenceResult, std::string> result) {
        m_inference_in_flight.store(false);
        on_debug_override_done(cycle, std::move(result));
    };
    job.enqueued_at = std::chrono::steady_clock::now();

    // m_inference_in_flight is already set true by push_rgb_frame under the
    // buffer lock. Reset only on submission failure.
    if (!m_queue->submit(std::move(job)))
    {
        m_inference_in_flight.store(false);
        return;
    }
    m_batches_submitted.fetch_add(1, std::memory_order_relaxed);
}

void EventCheckRunner::on_debug_override_done(std::shared_ptr<DebugOverrideCycle> cycle,
                                              tl::expected<InferenceResult, std::string> result)
{
    if (!result)
    {
        std::cout << "──── VLM debug-override FAILED ────\n"
                  << "  error: " << result.error() << "\n"
                  << "────────────────────────────────────\n"
                  << std::flush;
        HAILO_ANALYTICS_LOG_ERROR("EventCheckRunner: debug-override inference failed: {}", result.error());
        return;
    }
    const auto &inference_result = result.value();
    std::cout << "──── VLM debug-override result ────\n"
              << "  prompt       : " << cycle->prompt << "\n"
              << "  max_tokens   : " << cycle->max_tokens << "\n"
              << "  raw response : " << inference_result.response << "\n"
              << "  ttft_ms      : " << static_cast<int>(inference_result.stats.ttft_ms) << "\n"
              << "  total_ms     : " << static_cast<int>(inference_result.stats.total_ms) << "\n"
              << "  tps          : " << inference_result.stats.tps << "\n"
              << "  ctx_tokens   : " << inference_result.stats.context_usage << " / "
              << inference_result.stats.context_capacity << "\n"
              << "────────────────────────────────────\n"
              << std::flush;

    save_debug_override_metadata(*cycle, inference_result);
}

// ─── Metadata save helpers ──────────────────────────────────────────────────

namespace
{
// "2026-05-04T10:42:30.512Z" formatted from a system_clock timepoint.
std::string format_iso_now()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = static_cast<int>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buffer;
}

nlohmann::json build_stats_json(const VlmGenerationStats &stats)
{
    return {
        {"ttft_ms", stats.ttft_ms},
        {"total_ms", stats.total_ms},
        {"tps", stats.tps},
        {"tokens_generated", stats.tokens_generated},
        {"context_usage", stats.context_usage},
        {"context_capacity", stats.context_capacity},
    };
}

std::vector<std::string> frame_filenames(size_t count)
{
    std::vector<std::string> names;
    names.reserve(count);
    for (size_t index = 0; index < count; index++)
    {
        names.push_back("frame_" + std::to_string(index) + ".jpg");
    }
    return names;
}

template <typename Cycle>
void save_frames(const std::shared_ptr<MetadataWriter> &writer, const std::string &cycle_dir, const Cycle &cycle,
                 uint32_t input_height, uint32_t input_width)
{
    for (size_t index = 0; index < cycle.frames.size(); index++)
    {
        writer->save_frame_jpeg(cycle_dir, index, cycle.frames[index], input_height, input_width);
    }
}

// In-memory variant of save_frames. Returns one JPEG byte vector per frame
// for inclusion in the SSE-inline debug bundle. Empty entries indicate
// per-frame encode failures; the caller can decide whether to ship the
// (incomplete) bundle.
template <typename Cycle>
std::vector<std::vector<uint8_t>> encode_frames_in_memory(const std::shared_ptr<MetadataWriter> &writer,
                                                          const Cycle &cycle, uint32_t input_height,
                                                          uint32_t input_width)
{
    std::vector<std::vector<uint8_t>> result;
    result.reserve(cycle.frames.size());
    for (const auto &rgb : cycle.frames)
    {
        result.push_back(writer->encode_rgb_to_jpeg(rgb, input_height, input_width));
    }
    return result;
}
} // namespace

std::optional<TriggeredEvent::DebugBundle> EventCheckRunner::save_performance_metadata(
    const PerformanceCycle &cycle, const InferenceResult &result, const std::vector<bool> &verdicts)
{
    if (!m_metadata_writer || cycle.frames.empty())
    {
        return std::nullopt;
    }

    auto manager = m_queue->manager();
    if (!manager)
    {
        return std::nullopt;
    }
    const uint32_t height = manager->input_frame_height();
    const uint32_t width = manager->input_frame_width();
    const uint32_t channels = manager->input_frame_channels();

    const std::string cycle_dir = m_metadata_writer->begin_cycle();
    if (cycle_dir.empty())
    {
        return std::nullopt;
    }
    save_frames(m_metadata_writer, cycle_dir, cycle, height, width);

    nlohmann::json verdict_array = nlohmann::json::array();
    for (size_t index = 0; index < cycle.events.size(); index++)
    {
        verdict_array.push_back({
            {"id", cycle.events[index].id},
            {"description", cycle.events[index].description},
            {"yes", index < verdicts.size() && verdicts[index]},
        });
    }

    nlohmann::json frames_attached = nlohmann::json::array();
    for (size_t index = 0; index < cycle.frames.size(); index++)
    {
        frames_attached.push_back(index);
    }

    nlohmann::json document = {
        {"cycle_id", std::filesystem::path(cycle_dir).filename().string()},
        {"mode", "performance"},
        {"started_at", format_iso_now()},
        {"vlm_config",
         {
             {"hef_path", m_config.vlm_hef_path},
             {"do_sample", false},
             {"default_max_generated_tokens", m_config.vlm_default_max_generated_tokens},
             {"input_height", height},
             {"input_width", width},
             {"input_channels", channels},
         }},
        {"lead_prompt", m_config.performance_lead_prompt},
        {"frames", frame_filenames(cycle.frames.size())},
        {"inferences", nlohmann::json::array({{
                           {"kind", "performance_aggregate"},
                           {"prompt", cycle.prompt},
                           {"max_tokens", cycle.max_tokens},
                           {"use_video_mode", cycle.frames.size() > 1},
                           {"frames_attached", frames_attached},
                           {"response", result.response},
                           {"verdicts", verdict_array},
                           {"stats", build_stats_json(result.stats)},
                       }})},
    };
    m_metadata_writer->save_metadata_json(cycle_dir, document.dump(2));

    // Build the SSE-inline DebugBundle from the same data (in-memory JPEG
    // encode of the same RGB frames + the JSON document we just persisted).
    TriggeredEvent::DebugBundle bundle;
    bundle.cycle_id = std::filesystem::path(cycle_dir).filename().string();
    bundle.metadata = std::move(document);
    bundle.frames_336_jpeg = encode_frames_in_memory(m_metadata_writer, cycle, height, width);
    return bundle;
}

std::optional<TriggeredEvent::DebugBundle> EventCheckRunner::save_accuracy_metadata(const AccuracyCycle &cycle)
{
    if (!m_metadata_writer || cycle.frames.empty())
    {
        return std::nullopt;
    }

    auto manager = m_queue->manager();
    if (!manager)
    {
        return std::nullopt;
    }
    const uint32_t height = manager->input_frame_height();
    const uint32_t width = manager->input_frame_width();
    const uint32_t channels = manager->input_frame_channels();

    const std::string cycle_dir = m_metadata_writer->begin_cycle();
    if (cycle_dir.empty())
    {
        return std::nullopt;
    }
    save_frames(m_metadata_writer, cycle_dir, cycle, height, width);

    nlohmann::json frames_attached = nlohmann::json::array();
    for (size_t index = 0; index < cycle.frames.size(); index++)
    {
        frames_attached.push_back(index);
    }

    nlohmann::json inferences = nlohmann::json::array();

    // Description entry.
    {
        nlohmann::json entry = {
            {"kind", "description"},
            {"prompt", m_config.accuracy_lead_prompt},
            {"max_tokens", m_config.accuracy_max_tokens},
            {"use_video_mode", cycle.frames.size() > 1},
            {"frames_attached", frames_attached},
        };
        if (cycle.description_ok)
        {
            entry["response"] = cycle.description_result.response;
            entry["stats"] = build_stats_json(cycle.description_result.stats);
        }
        else
        {
            entry["error"] = cycle.description_error;
        }
        inferences.push_back(std::move(entry));
    }

    // Per-event entries.
    size_t total_ctx_tokens = cycle.description_ok ? cycle.description_result.stats.context_usage : 0;
    for (size_t index = 0; index < cycle.events.size(); index++)
    {
        const auto &event = cycle.events[index];
        const bool ok = index < cycle.event_ok.size() && cycle.event_ok[index];
        nlohmann::json entry = {
            {"kind", "event"},
            {"event_id", event.id},
            {"event_description", event.description},
            {"max_tokens", m_config.max_generated_tokens_per_event},
            {"use_video_mode", false},
            {"frames_attached", nlohmann::json::array()},
        };
        if (ok)
        {
            const auto &er = cycle.event_results[index];
            entry["prompt"] = er.response.empty() ? std::string{} : std::string{}; // filled below
            // We don't have the exact prompt stored on the cycle, so reconstruct
            // the same way the runner builds it (must mirror submit_next_accuracy_inference).
            std::string desc = event.description;
            while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.back())))
            {
                desc.pop_back();
            }
            const std::string suffix =
                (!desc.empty() && desc.back() == '?') ? " just answer with Yes or No" : "? just answer with Yes or No";
            entry["prompt"] = desc + suffix;
            entry["response"] = er.response;
            entry["yes"] = (index < cycle.event_verdicts.size()) ? cycle.event_verdicts[index] : false;
            entry["stats"] = build_stats_json(er.stats);
            total_ctx_tokens += er.stats.context_usage;
        }
        else
        {
            entry["error"] = (index < cycle.event_errors.size()) ? cycle.event_errors[index] : std::string{};
        }
        inferences.push_back(std::move(entry));
    }

    const auto cycle_total_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cycle.cycle_start)
            .count();

    nlohmann::json document = {
        {"cycle_id", std::filesystem::path(cycle_dir).filename().string()},
        {"mode", "accuracy"},
        {"started_at", format_iso_now()},
        {"vlm_config",
         {
             {"hef_path", m_config.vlm_hef_path},
             {"do_sample", false},
             {"default_max_generated_tokens", m_config.vlm_default_max_generated_tokens},
             {"input_height", height},
             {"input_width", width},
             {"input_channels", channels},
         }},
        {"lead_prompt", m_config.accuracy_lead_prompt},
        {"frames", frame_filenames(cycle.frames.size())},
        {"inferences", inferences},
        {"cycle_totals",
         {
             {"total_ms", cycle_total_ms},
             {"total_context_tokens", total_ctx_tokens},
         }},
    };
    m_metadata_writer->save_metadata_json(cycle_dir, document.dump(2));

    // Build the SSE-inline DebugBundle (same content as the on-disk dir).
    TriggeredEvent::DebugBundle bundle;
    bundle.cycle_id = std::filesystem::path(cycle_dir).filename().string();
    bundle.metadata = std::move(document);
    bundle.frames_336_jpeg = encode_frames_in_memory(m_metadata_writer, cycle, height, width);
    return bundle;
}

void EventCheckRunner::save_debug_override_metadata(const DebugOverrideCycle &cycle, const InferenceResult &result)
{
    if (!m_metadata_writer || cycle.frames.empty())
    {
        return;
    }

    auto manager = m_queue->manager();
    if (!manager)
    {
        return;
    }
    const uint32_t height = manager->input_frame_height();
    const uint32_t width = manager->input_frame_width();
    const uint32_t channels = manager->input_frame_channels();

    const std::string cycle_dir = m_metadata_writer->begin_cycle();
    if (cycle_dir.empty())
    {
        return;
    }
    save_frames(m_metadata_writer, cycle_dir, cycle, height, width);

    nlohmann::json frames_attached = nlohmann::json::array();
    for (size_t index = 0; index < cycle.frames.size(); index++)
    {
        frames_attached.push_back(index);
    }

    nlohmann::json document = {
        {"cycle_id", std::filesystem::path(cycle_dir).filename().string()},
        {"mode", "debug_override"},
        {"started_at", format_iso_now()},
        {"vlm_config",
         {
             {"hef_path", m_config.vlm_hef_path},
             {"do_sample", false},
             {"default_max_generated_tokens", m_config.vlm_default_max_generated_tokens},
             {"input_height", height},
             {"input_width", width},
             {"input_channels", channels},
         }},
        {"lead_prompt", cycle.prompt},
        {"frames", frame_filenames(cycle.frames.size())},
        {"inferences", nlohmann::json::array({{
                           {"kind", "debug_override"},
                           {"prompt", cycle.prompt},
                           {"max_tokens", cycle.max_tokens},
                           {"use_video_mode", cycle.frames.size() > 1},
                           {"frames_attached", frames_attached},
                           {"response", result.response},
                           {"stats", build_stats_json(result.stats)},
                       }})},
    };
    m_metadata_writer->save_metadata_json(cycle_dir, document.dump(2));
}

} // namespace vlm_event_monitor
