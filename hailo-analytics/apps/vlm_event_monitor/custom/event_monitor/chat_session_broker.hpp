#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <tl/expected.hpp>

#include "vlm_frame_preprocessor.hpp"
#include "vlm_inference_manager.hpp"

namespace vlm_event_monitor
{

class InferenceRequestQueue;
class JpegRingBufferStage;
class SseBroadcaster;

// One per /chat/stream invocation. Pushed to by the on_token callback
// (queue worker thread). Drained by the cpp-httplib chunked provider
// lambda (cpp-httplib worker thread). Mirrors the cv+queue+dropped-sink
// pattern in SseBroadcaster::attach_client but for a single consumer
// — chat is per-request streaming, not fan-out.
class ChatStreamHandler
{
  public:
    // Called from the queue worker thread once per decoded token.
    void push_token(const std::string &token);

    // Called from the queue worker thread once when inference returns.
    void finalize(tl::expected<InferenceResult, std::string> result, std::chrono::milliseconds wait_ms,
                  uint32_t max_generated_tokens);

    // Called by the cpp-httplib chunked provider lambda. Blocks until
    // either tokens are available, finalize() has run, or the client is
    // marked disconnected. Returns the next pre-formatted SSE frame, or
    // an empty string if there's nothing more to write (caller should
    // return false to terminate the chunked provider).
    std::string next_frame_or_block();

    // Observed by the on_token callback to abort generation cleanly.
    void mark_client_disconnected();
    bool client_disconnected() const;

    // True once finalize() has run AND all queued frames have been drained.
    bool finished() const;

  private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_pending_frames; // pre-formatted SSE frames
    bool m_done = false;
    bool m_client_disconnected = false;
};

// Single-session chat broker. Manages the lifecycle of a chat session on
// top of VlmInferenceManager + InferenceRequestQueue.
//
// Threading:
//   - Public API is thread-safe. The webserver calls start_*/run_stream/
//     close_session from cpp-httplib worker threads.
//   - One internal janitor thread evicts idle sessions and pushes the
//     `chat_session_expired` SSE event.
//   - Inference runs on the queue's worker thread (one job at a time).
class ChatSessionBroker
{
  public:
    struct Config
    {
        uint32_t session_timeout_seconds = 300;
        bool pause_event_check_during_chat = true;
        uint32_t default_max_generated_tokens = 256;
    };

    ChatSessionBroker(std::shared_ptr<InferenceRequestQueue> queue, std::shared_ptr<JpegRingBufferStage> jpeg_ring,
                      std::shared_ptr<VlmFramePreprocessor> preprocessor,
                      std::shared_ptr<SseBroadcaster> sse_broadcaster, Config config);
    ~ChatSessionBroker();

    ChatSessionBroker(const ChatSessionBroker &) = delete;
    ChatSessionBroker &operator=(const ChatSessionBroker &) = delete;

    void start();
    void stop();

    // Wire the JPEG ring after construction. The pipeline builds the ring
    // in build_pipeline(), which runs after register_app_extensions where
    // the broker is constructed. Calling this before any /chat/start is
    // mandatory for live-chat to work.
    void set_jpeg_ring(std::shared_ptr<JpegRingBufferStage> jpeg_ring);

    struct StartResult
    {
        uint32_t session_id = 0;
        size_t frame_count = 0;
        std::string snapshot_base64; // populated for live-chat only
    };

    // POST /chat/start (event-chat) — caller has already decoded base64
    // frames into JPEG byte arrays. Auto-evicts any existing session.
    tl::expected<StartResult, std::string> start_event_chat(const std::vector<std::vector<uint8_t>> &jpeg_frames);

    // POST /chat/start (live-chat) — broker grabs JpegRingBufferStage::latest()
    // itself, returns it as base64 in the response so the modal can render.
    // Auto-evicts any existing session.
    tl::expected<StartResult, std::string> start_live_chat();

    // POST /chat/stream — runs inference and streams tokens through `handler`.
    // Returns immediately after submitting to the queue; the caller's
    // chunked content provider drives the handler thereafter.
    tl::expected<void, std::string> run_stream(uint32_t session_id, const std::string &prompt,
                                               uint32_t max_generated_tokens,
                                               std::shared_ptr<ChatStreamHandler> handler);

    // POST /chat/close.
    tl::expected<void, std::string> close_session(uint32_t session_id);

    // True if any chat session is currently open. Used by the
    // webserver's build_monitoring_status() to distinguish a chat-active
    // pause from an explicit Stop.
    bool any_session_open() const;

    // Called once on every event_inference_enabled transition driven by
    // chat open/close. Used by the pipeline to push an SSE
    // monitoring_status update so the frontend's Monitoring badge can
    // switch between green "running" and amber "paused — chat active"
    // live. Invoked from the cpp-httplib worker thread (the thread that
    // called start_*/close_session)
    void set_pause_state_change_notifier(std::function<void()> notifier);

  private:
    struct ChatSession
    {
        uint32_t session_id = 0;
        std::vector<std::vector<uint8_t>> stashed_rgb_frames; // consumed on first stream
        bool first_inference_done = false;
        std::chrono::steady_clock::time_point last_activity_at;
        std::weak_ptr<ChatStreamHandler> active_handler;
    };

    void janitor_loop();

    // Evict every session in m_sessions. Caller must hold m_mutex.
    // After return, m_sessions is empty and the event-check toggle is
    // updated. Blocks on the manager mutex until any in-flight inference
    // aborts.
    void evict_all_locked();

    // Update the event-check toggle based on whether m_sessions is empty.
    // Caller must hold m_mutex. Only flips the queue and m_chat_pause_active
    // on actual transitions; redundant calls are a no-op.
    void on_session_count_changed_locked();

    // Invoke the registered pause notifier (no-op if unset). Safe to call
    // without holding m_mutex.
    void fire_pause_notifier();

    // RAII guard: snapshots m_chat_pause_active at construction; on
    // destruction (after the public method has unwound and m_mutex is
    // released) compares against the current value and fires the pause
    // notifier exactly once if a transition happened. Always declare at
    // the TOP of the public entry function (before any lock_guard) so
    // the guard's dtor runs AFTER the lock is released.
    class PauseNotifyOnExit
    {
      public:
        explicit PauseNotifyOnExit(ChatSessionBroker &broker)
            : m_broker(broker), m_initial(broker.m_chat_pause_active.load())
        {
        }
        ~PauseNotifyOnExit()
        {
            if (m_broker.m_chat_pause_active.load() != m_initial)
            {
                m_broker.fire_pause_notifier();
            }
        }
        PauseNotifyOnExit(const PauseNotifyOnExit &) = delete;
        PauseNotifyOnExit &operator=(const PauseNotifyOnExit &) = delete;

      private:
        ChatSessionBroker &m_broker;
        bool m_initial;
    };

    // Preprocess JPEG bytes → 336x336 RGB, returning {rgb_frames, error}.
    tl::expected<std::vector<std::vector<uint8_t>>, std::string> preprocess_jpegs(
        const std::vector<std::vector<uint8_t>> &jpegs) const;

    std::shared_ptr<InferenceRequestQueue> m_queue;
    std::shared_ptr<JpegRingBufferStage> m_jpeg_ring;
    std::shared_ptr<VlmFramePreprocessor> m_preprocessor;
    std::shared_ptr<SseBroadcaster> m_sse_broadcaster;
    Config m_config;

    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, std::shared_ptr<ChatSession>> m_sessions;

    std::atomic<bool> m_running{false};
    std::thread m_janitor;
    std::mutex m_janitor_mutex;
    std::condition_variable m_janitor_cv;

    std::atomic<bool> m_chat_pause_active{false};

    mutable std::mutex m_pause_notifier_mutex;
    std::function<void()> m_pause_notifier;
};

} // namespace vlm_event_monitor
