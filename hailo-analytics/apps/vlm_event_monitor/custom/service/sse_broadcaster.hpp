#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include "../event_monitor/event_check_runner.hpp"

namespace httplib
{
class Response;
class DataSink;
} // namespace httplib

namespace vlm_event_monitor
{

// Snapshot of monitoring state. Pushed via SSE on every replace_all and on
// every successful start/stop. The webserver also synthesises one when a
// new client connects, so the UI can populate counters without waiting for
// the next state change.
struct MonitoringStatus
{
    std::string state = "running"; // "running" | "stopped"
    std::string mode = "performance";
    size_t event_count = 0;
    size_t enabled_count = 0;
    std::string busy_with = "idle"; // mirrors InferenceRequestQueue::busy_with()
    size_t pending_count = 0;
    bool event_inference_enabled = true;
    // number of distinct event_ids currently inside their
    // cooldown window. Populated from EventStateTracker::active_incident_count().
    size_t active_incidents = 0;
    // Deferred VLM load surface.
    //   vlm_state: "loading" | "ready" | "failed"
    //   vlm_error: populated only when vlm_state == "failed"
    //   monitoring_pause_reason: "" | "vlm_loading" | "vlm_failed" | "chat_active"
    // The frontend renders the Monitoring badge from the combined
    // (state, vlm_state, monitoring_pause_reason) triple.
    std::string vlm_state = "loading";
    std::string vlm_error;
    std::string monitoring_pause_reason;
    // Current EventStateTracker cooldown (seconds). Drives the
    // Settings-modal slider so the frontend can render the right initial
    // position + react to runtime changes from other tabs. While VLM is
    // still loading and the tracker doesn't yet exist, this carries the
    // YAML default so the slider has something sensible to show.
    uint32_t cooldown_seconds = 60;
};

// Multi-client SSE fan-out. One instance per app. The webserver registers
// each new EventSource client via attach_client(); the runner pushes
// triggered events via push_new_event(); a single timer thread emits a
// 15 s heartbeat
//
// SSE wire format: every event is encoded as
//     event: <name>\ndata: <json>\n\n
// (json is a single-line nlohmann::json::dump()). We never split a JSON
// object across multiple lines.
class SseBroadcaster
{
  public:
    SseBroadcaster();
    ~SseBroadcaster();

    SseBroadcaster(const SseBroadcaster &) = delete;
    SseBroadcaster &operator=(const SseBroadcaster &) = delete;

    // Wire a new client. Sets the response's chunked content provider; the
    // provider blocks on a per-client condition variable until either a
    // new event is queued or the heartbeat fires. Returns immediately;
    // cpp-httplib drives the lambda thereafter.
    //
    // Optionally takes an initial monitoring_status payload to greet the
    // client with (so the UI can populate state without waiting for the
    // next change).
    void attach_client(httplib::Response &res, const MonitoringStatus &initial_status);

    // Producer hooks — safe to call from any thread.
    void push_new_event(const TriggeredEvent &event);
    void push_monitoring_status(const MonitoringStatus &status);

    // Notify all subscribers that a chat session has been ended on the
    // backend (currently only the idle-timeout janitor uses it). The
    // frontend ChatModal filters by `session_id` against its own
    // currentSessionId and renders the message as a system bubble.
    //
    // SSE wire format:
    //   event: chat_session_expired
    //   data: {"session_id": <id>, "reason": "<reason>", "message": "<text>"}
    void push_chat_session_expired(uint32_t session_id, const std::string &reason, const std::string &message);

  private:
    struct Client
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::string> queue; // pre-formatted SSE frames
        bool dropped = false;          // set true when the sink writes fail or shutdown
    };

    void heartbeat_loop();
    static std::string format_frame(const std::string &event_name, const std::string &json);
    void enqueue_for_all(std::string frame);

    std::mutex m_clients_mutex;
    std::unordered_set<std::shared_ptr<Client>> m_clients;

    std::atomic<bool> m_running{true};
    std::atomic<uint64_t> m_next_event_id{1};
    std::thread m_heartbeat_thread;
};

} // namespace vlm_event_monitor
