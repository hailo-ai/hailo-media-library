#include "sse_broadcaster.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_event_monitor
{

namespace
{

constexpr std::chrono::seconds kHeartbeatInterval{15};

// Standard library base64 alphabet (table-driven encoder for raw bytes).
const std::string kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t *data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t index = 0;
    while (index + 3 <= len)
    {
        const uint32_t triple = (static_cast<uint32_t>(data[index]) << 16) |
                                (static_cast<uint32_t>(data[index + 1]) << 8) | static_cast<uint32_t>(data[index + 2]);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[triple & 0x3F]);
        index += 3;
    }
    if (index < len)
    {
        const uint32_t b0 = data[index];
        const uint32_t b1 = (index + 1 < len) ? data[index + 1] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back((index + 1 < len) ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

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

std::string format_iso(std::chrono::system_clock::time_point tp)
{
    using namespace std::chrono;
    const auto t = system_clock::to_time_t(tp);
    const auto ms = static_cast<int>(duration_cast<milliseconds>(tp.time_since_epoch()).count() % 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buffer;
}

nlohmann::json status_to_json(const MonitoringStatus &status)
{
    return {
        {"state", status.state},
        {"mode", status.mode},
        {"event_count", status.event_count},
        {"enabled_count", status.enabled_count},
        {"busy_with", status.busy_with},
        {"pending_count", status.pending_count},
        {"event_inference_enabled", status.event_inference_enabled},
        {"active_incidents", status.active_incidents},
        {"vlm_state", status.vlm_state},
        {"vlm_error", status.vlm_error},
        {"monitoring_pause_reason", status.monitoring_pause_reason},
        {"cooldown_seconds", status.cooldown_seconds},
    };
}

} // namespace

SseBroadcaster::SseBroadcaster()
{
    m_heartbeat_thread = std::thread(&SseBroadcaster::heartbeat_loop, this);
}

SseBroadcaster::~SseBroadcaster()
{
    m_running.store(false);

    // Wake every per-client wait so they can observe `dropped`.
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (const auto &client : m_clients)
        {
            std::lock_guard<std::mutex> client_lock(client->mutex);
            client->dropped = true;
            client->cv.notify_all();
        }
    }

    if (m_heartbeat_thread.joinable())
    {
        m_heartbeat_thread.join();
    }
}

std::string SseBroadcaster::format_frame(const std::string &event_name, const std::string &json)
{
    std::string frame;
    frame.reserve(event_name.size() + json.size() + 16);
    frame.append("event: ").append(event_name).append("\n");
    frame.append("data: ").append(json).append("\n\n");
    return frame;
}

void SseBroadcaster::enqueue_for_all(std::string frame)
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    for (const auto &client : m_clients)
    {
        std::lock_guard<std::mutex> client_lock(client->mutex);
        if (client->dropped)
        {
            continue;
        }
        // Cap per-client backlog to avoid unbounded memory if a client
        // stalls. 64 frames is plenty even for a chatty event-check loop.
        constexpr size_t kMaxQueue = 64;
        if (client->queue.size() >= kMaxQueue)
        {
            client->queue.pop_front();
        }
        client->queue.push_back(frame);
        client->cv.notify_one();
    }
}

void SseBroadcaster::push_new_event(const TriggeredEvent &event)
{
    nlohmann::json frames_array = nlohmann::json::array();
    for (const auto &jpeg : event.frames)
    {
        frames_array.push_back(base64_encode(jpeg.data(), jpeg.size()));
    }
    nlohmann::json payload = {
        {"id", m_next_event_id.fetch_add(1, std::memory_order_relaxed)},
        {"event_id", event.event_id},
        {"event_description", event.event_description},
        {"vlm_description", event.vlm_description},
        {"timestamp", format_iso(event.timestamp)},
        {"frame_count", frames_array.size()},
        {"frames", frames_array},
    };

    // Optional debug bundle (336x336 JPEGs + on-disk-shaped
    // metadata.json). Only present when debug_metadata_save_enabled is true
    // in YAML; the frontend hides its 3-dot download button when absent.
    if (event.debug_bundle.has_value())
    {
        nlohmann::json frames_obj = nlohmann::json::object();
        for (size_t index = 0; index < event.debug_bundle->frames_336_jpeg.size(); index++)
        {
            const auto &jpeg = event.debug_bundle->frames_336_jpeg[index];
            if (jpeg.empty())
            {
                continue; // per-frame encode failed earlier; logged at source
            }
            const std::string filename = "frame_" + std::to_string(index) + ".jpg";
            frames_obj[filename] = base64_encode(jpeg.data(), jpeg.size());
        }
        payload["debug"] = {
            {"cycle_id", event.debug_bundle->cycle_id},
            {"metadata", event.debug_bundle->metadata},
            {"frames", frames_obj},
        };
    }

    enqueue_for_all(format_frame("new_event", payload.dump()));
}

void SseBroadcaster::push_monitoring_status(const MonitoringStatus &status)
{
    enqueue_for_all(format_frame("monitoring_status", status_to_json(status).dump()));
}

void SseBroadcaster::push_chat_session_expired(uint32_t session_id, const std::string &reason,
                                               const std::string &message)
{
    nlohmann::json payload = {
        {"session_id", session_id},
        {"reason", reason},
        {"message", message},
    };
    enqueue_for_all(format_frame("chat_session_expired", payload.dump()));
}

void SseBroadcaster::heartbeat_loop()
{
    while (m_running.load())
    {
        std::this_thread::sleep_for(kHeartbeatInterval);
        if (!m_running.load())
        {
            break;
        }
        nlohmann::json payload = {{"ts", format_iso_now()}};
        enqueue_for_all(format_frame("heartbeat", payload.dump()));
    }
}

void SseBroadcaster::attach_client(httplib::Response &res, const MonitoringStatus &initial_status)
{
    auto client = std::make_shared<Client>();

    // Greeting frame: one monitoring_status so the UI can paint its
    // counters immediately without waiting for the next change.
    {
        std::lock_guard<std::mutex> lock(client->mutex);
        client->queue.push_back(format_frame("monitoring_status", status_to_json(initial_status).dump()));
    }

    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        m_clients.insert(client);
    }

    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no"); // disable proxy buffering if any
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, client](size_t /*offset*/, httplib::DataSink &sink) -> bool {
            std::unique_lock<std::mutex> lock(client->mutex);
            // Block (with a max wait so we can observe shutdown) until
            // either an event is queued or the broadcaster stops.
            client->cv.wait_for(lock, std::chrono::seconds(1), [&client, this] {
                return !client->queue.empty() || client->dropped || !m_running.load();
            });
            if (client->dropped || !m_running.load())
            {
                sink.done();
                return false;
            }
            while (!client->queue.empty())
            {
                std::string frame = std::move(client->queue.front());
                client->queue.pop_front();
                lock.unlock();
                if (!sink.write(frame.data(), frame.size()))
                {
                    // Client gone — mark dropped so the lambda exits next tick.
                    lock.lock();
                    client->dropped = true;
                    return false;
                }
                lock.lock();
            }
            return true;
        },
        [this, client](bool /*success*/) {
            // Resource cleanup once cpp-httplib finishes the stream.
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_clients.erase(client);
        });
}

} // namespace vlm_event_monitor
