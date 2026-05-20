#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <tl/expected.hpp>

#include "vlm_inference_manager.hpp"

namespace vlm_event_monitor
{

// Priority axis for the inference queue. EventCheck always runs ahead of Chat
// when both are pending. There is intentionally no priority distinction
// between events themselves (locked decision 8).
enum class InferencePriority
{
    EventCheck = 0, // higher priority
    Chat = 1,
};

// A unit of work for the inference queue. Caller wraps either a one-shot
// (event-check) or a session-based call (chat) into `work`. The queue runs
// it on the manager held by the queue, then dispatches `on_complete`.
struct InferenceJob
{
    InferencePriority priority{InferencePriority::EventCheck};
    std::function<tl::expected<InferenceResult, std::string>(VlmInferenceManager &)> work;
    std::function<void(tl::expected<InferenceResult, std::string>)> on_complete;
    std::chrono::steady_clock::time_point enqueued_at{std::chrono::steady_clock::now()};
};

// Single-consumer priority queue that owns the VlmInferenceManager and
// serialises all inference calls through it. EventCheck submissions can be
// disabled at runtime via set_event_inference_enabled(false), e.g. when a
// chat session is open and the YAML toggle requests pausing monitoring.
class InferenceRequestQueue
{
  public:
    explicit InferenceRequestQueue(std::shared_ptr<VlmInferenceManager> manager);
    ~InferenceRequestQueue();

    InferenceRequestQueue(const InferenceRequestQueue &) = delete;
    InferenceRequestQueue &operator=(const InferenceRequestQueue &) = delete;

    void start();
    void stop();

    // Submits a job. Returns false if `priority == EventCheck` and event-check
    // submissions are currently disabled. The on_complete callback is always
    // dispatched on the queue's worker thread.
    bool submit(InferenceJob job);

    // Drop subsequent EventCheck submissions when set to false. Chat
    // submissions are unaffected.
    void set_event_inference_enabled(bool enabled);
    bool is_event_inference_enabled() const;

    size_t pending_count() const;

    // Returns "idle" | "event_check" | "chat".
    std::string busy_with() const;

    std::shared_ptr<VlmInferenceManager> manager() const
    {
        return m_manager;
    }

  private:
    void worker_loop();

    std::shared_ptr<VlmInferenceManager> m_manager;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<InferenceJob> m_event_queue;
    std::deque<InferenceJob> m_chat_queue;

    std::atomic<bool> m_event_inference_enabled{true};
    std::atomic<bool> m_running{false};
    std::thread m_worker;

    // 0 = idle, 1 = running EventCheck, 2 = running Chat.
    std::atomic<int> m_current_busy_state{0};
};

} // namespace vlm_event_monitor
