#include "webserver.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

#include "custom/event_monitor/chat_session_broker.hpp"
#include "custom/event_monitor/event_check_runner.hpp"
#include "custom/event_monitor/event_store.hpp"
#include "custom/service/sse_broadcaster.hpp"
#include "custom/streaming/webrtc_streamer_ext.hpp"
#include "vlm_pipeline.hpp"
#include "vlm_pipeline_defines.hpp"

using json = nlohmann::json;
using hailo_analytics::analytics::app_constructor::AppConfigOverride;
using hailo_analytics::analytics::app_constructor::CameraAppConstructor;
using vlm_event_monitor::EventCheckMode;
using vlm_event_monitor::MonitoringStatus;
using vlm_event_monitor::UserEvent;

IntegratedWebServer::IntegratedWebServer(const vlm_app_config::VlmAppConfig &config)
    : m_server(std::make_unique<httplib::Server>()), m_config(config)
{
}

tl::expected<std::shared_ptr<IntegratedWebServer>, std::string> IntegratedWebServer::create(
    const vlm_app_config::VlmAppConfig &config)
{
    auto instance = std::shared_ptr<IntegratedWebServer>(new IntegratedWebServer(config));

    instance->setup_routes();
    instance->setup_cors();

    AppConfigOverride app_config_override;
    app_config_override.m_user_data = std::make_shared<VlmAppCustomData>(config);

    auto app_result = CameraAppConstructor::create<VlmEventMonitorPipeline>(app_config_override);
    if (!app_result)
    {
        return tl::make_unexpected("Failed to create VlmEventMonitorPipeline");
    }
    instance->m_app = app_result.value();

    auto webrtc_ext = instance->m_app->get_extension<WebRTCStreamerExt>();
    if (!webrtc_ext)
    {
        return tl::make_unexpected("WebRTCStreamerExt extension is required but was not registered");
    }
    instance->m_webrtc_streamer_ext = webrtc_ext;

    // The webserver borrows shared_ptrs to wire the new routes.
    instance->m_event_store = instance->m_app->event_store();
    instance->m_sse_broadcaster = instance->m_app->sse_broadcaster();
    if (!instance->m_event_store || !instance->m_sse_broadcaster)
    {
        return tl::make_unexpected("EventStore / SseBroadcaster were not constructed by the pipeline");
    }

    return instance;
}

void IntegratedWebServer::start(std::string host, int port)
{
    HAILO_ANALYTICS_LOG_INFO("Starting VLM Event Monitor pipeline...");
    auto start_result = m_app->start();
    if (!start_result)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to start VlmEventMonitorPipeline");
        return;
    }

    HAILO_ANALYTICS_LOG_INFO("Listening on http://{}:{}", host, port);
    m_server->listen(host, port);
}

void IntegratedWebServer::stop()
{
    if (m_server)
    {
        m_server->stop();
    }
    if (m_app)
    {
        m_app->stop();
        m_app->release();
    }
}

void IntegratedWebServer::setup_cors()
{
    m_server->set_pre_routing_handler([](const httplib::Request & /*req*/, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    m_server->Options(".*", [](const httplib::Request &, httplib::Response & /*res*/) { return; });
}

void IntegratedWebServer::setup_routes()
{
    // Serve every file in the webfrontend directory as a static asset.
    // Specific Get/Post handlers below take precedence over the mount, so
    // GET / continues to be served by setup_root_route. Anything without a
    // specific route (jszip.min.js, future CSS/JS, …) falls through here.
    if (!m_server->set_mount_point("/", vlm_app::paths::webfrontend_dir))
    {
        HAILO_ANALYTICS_LOG_WARN("IntegratedWebServer: failed to mount webfrontend dir at /: {}",
                                 vlm_app::paths::webfrontend_dir);
    }

    setup_root_route();
    setup_status_route();
    setup_webrtc_offer_route();
    setup_webrtc_answer_route();
    setup_webrtc_ice_candidate_route();
    setup_webrtc_status_route();
    setup_webrtc_options_route();
    setup_events_routes();
    setup_monitoring_status_route();
    setup_monitoring_config_route();
    setup_triggered_events_stream_route();
    setup_chat_routes();
}

void IntegratedWebServer::setup_root_route()
{
    m_server->Get("/", [this](const httplib::Request & /*req*/, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("GET /");
        serve_index_html(res);
    });
}

void IntegratedWebServer::setup_status_route()
{
    m_server->Get("/api/status", [](const httplib::Request & /*req*/, httplib::Response &res) {
        json response;
        response["status"] = "ok";
        response["app"] = "vlm_event_monitor";
        response["timestamp"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        res.set_content(response.dump(), "application/json");
    });
}

void IntegratedWebServer::setup_webrtc_offer_route()
{
    m_server->Post("/api/webrtc/offer", [this](const httplib::Request & /*req*/, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("POST /api/webrtc/offer");
        try
        {
            std::string offer = m_webrtc_streamer_ext->create_offer();
            res.set_content(offer, "application/json");
        }
        catch (const std::exception &e)
        {
            res.status = 500;
            json err{{"status", "error"}, {"message", "Internal server error"}, {"details", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });
}

void IntegratedWebServer::setup_webrtc_answer_route()
{
    m_server->Post("/api/webrtc/answer", [this](const httplib::Request &req, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("POST /api/webrtc/answer");
        try
        {
            json body = json::parse(req.body);
            m_webrtc_streamer_ext->handle_answer(body["sdp"]);
            res.set_content("OK", "text/plain");
        }
        catch (const std::exception &e)
        {
            res.status = 500;
            json err{{"status", "error"}, {"message", "Internal server error"}, {"details", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });
}

void IntegratedWebServer::setup_webrtc_ice_candidate_route()
{
    m_server->Post("/api/webrtc/ice-candidate", [this](const httplib::Request &req, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("POST /api/webrtc/ice-candidate");
        try
        {
            json body = json::parse(req.body);
            m_webrtc_streamer_ext->handle_ice_candidate(body["candidate"], body["sdpMid"], body["sdpMLineIndex"]);
            res.set_content("OK", "text/plain");
        }
        catch (const std::exception &e)
        {
            res.status = 500;
            json err{{"status", "error"}, {"message", "Internal server error"}, {"details", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });
}

void IntegratedWebServer::setup_webrtc_status_route()
{
    m_server->Get("/api/webrtc/status", [this](const httplib::Request & /*req*/, httplib::Response &res) {
        json status;
        status["connected"] = !m_webrtc_streamer_ext->is_connection_closed();
        res.set_content(status.dump(), "application/json");
    });
}

void IntegratedWebServer::setup_webrtc_options_route()
{
    m_server->Options("/api/webrtc/.*", [](const httplib::Request &, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });
}

// ─── Events CRUD, monitoring status, SSE ──────────────────────────

namespace
{
EventCheckMode mode_from_string(const std::string &raw, EventCheckMode fallback)
{
    std::string lower;
    lower.reserve(raw.size());
    for (char character : raw)
    {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (lower == "performance")
    {
        return EventCheckMode::Performance;
    }
    if (lower == "accuracy")
    {
        return EventCheckMode::Accuracy;
    }
    return fallback;
}

json events_to_json(const std::vector<UserEvent> &events, EventCheckMode mode, bool file_existed)
{
    json events_array = json::array();
    for (const auto &event : events)
    {
        events_array.push_back({
            {"id", event.id},
            {"description", event.description},
            {"enabled", event.enabled},
        });
    }
    return {
        {"events", events_array},
        {"mode", vlm_event_monitor::to_string(mode)},
        {"file_existed", file_existed},
    };
}

// Returns empty optional + sets res status/body on parse failure.
struct ReplaceAllPayload
{
    std::vector<UserEvent> events;
    EventCheckMode mode = EventCheckMode::Performance;
};

bool parse_replace_all_body(const std::string &body, ReplaceAllPayload &out, std::string &error)
{
    try
    {
        json doc = json::parse(body);
        if (!doc.contains("events") || !doc["events"].is_array())
        {
            error = "missing or non-array 'events'";
            return false;
        }
        if (!doc.contains("mode") || !doc["mode"].is_string())
        {
            error = "missing or non-string 'mode'";
            return false;
        }
        out.mode = mode_from_string(doc["mode"].get<std::string>(), EventCheckMode::Performance);
        for (const auto &node : doc["events"])
        {
            UserEvent event;
            if (!node.contains("id") || !node["id"].is_number_integer())
            {
                error = "event missing integer 'id'";
                return false;
            }
            if (!node.contains("description") || !node["description"].is_string())
            {
                error = "event missing string 'description'";
                return false;
            }
            if (!node.contains("enabled") || !node["enabled"].is_boolean())
            {
                error = "event missing boolean 'enabled'";
                return false;
            }
            event.id = node["id"].get<uint32_t>();
            event.description = node["description"].get<std::string>();
            event.enabled = node["enabled"].get<bool>();
            out.events.push_back(std::move(event));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error = std::string{"JSON parse error: "} + e.what();
        return false;
    }
}
} // namespace

MonitoringStatus IntegratedWebServer::build_monitoring_status() const
{
    if (!m_app)
    {
        MonitoringStatus status;
        status.state = "stopped";
        status.vlm_state = "loading";
        status.monitoring_pause_reason = "vlm_loading";
        return status;
    }
    return m_app->build_monitoring_status();
}

bool IntegratedWebServer::is_vlm_ready_or_503(httplib::Response &res) const
{
    if (!m_app)
    {
        res.status = 503;
        res.set_content(json{{"error", "vlm_loading"}, {"state", "loading"}, {"detail", ""}}.dump(),
                        "application/json");
        return false;
    }
    switch (m_app->vlm_load_state())
    {
    case VlmEventMonitorPipeline::VlmLoadState::Ready:
        return true;
    case VlmEventMonitorPipeline::VlmLoadState::Loading:
        res.status = 503;
        res.set_content(json{{"error", "vlm_loading"}, {"state", "loading"}, {"detail", ""}}.dump(),
                        "application/json");
        return false;
    case VlmEventMonitorPipeline::VlmLoadState::Failed:
        res.status = 503;
        res.set_content(json{{"error", "vlm_failed"}, {"state", "failed"}, {"detail", m_app->vlm_load_error()}}.dump(),
                        "application/json");
        return false;
    }
    return false;
}

void IntegratedWebServer::setup_events_routes()
{
    // GET /api/events — read the events YAML straight from disk (drives both
    // initial page-load and the Load button). Missing file → 200 with
    // empty-list sentinel.
    m_server->Get("/api/events", [this](const httplib::Request & /*req*/, httplib::Response &res) {
        if (!m_event_store)
        {
            res.status = 503;
            res.set_content(json{{"status", "error"}, {"message", "EventStore not available"}}.dump(),
                            "application/json");
            return;
        }
        const auto snapshot = m_event_store->read_from_disk();
        res.set_content(events_to_json(snapshot.events, snapshot.mode, snapshot.file_existed).dump(),
                        "application/json");
    });

    // PUT /api/events — atomic replace + persist + apply.
    m_server->Put("/api/events", [this](const httplib::Request &req, httplib::Response &res) {
        if (!m_event_store)
        {
            res.status = 503;
            res.set_content(json{{"status", "error"}, {"message", "EventStore not available"}}.dump(),
                            "application/json");
            return;
        }

        ReplaceAllPayload payload;
        std::string parse_error;
        if (!parse_replace_all_body(req.body, payload, parse_error))
        {
            res.status = 400;
            res.set_content(json{{"status", "error"}, {"reason", parse_error}}.dump(), "application/json");
            return;
        }

        auto result = m_event_store->replace_all(std::move(payload.events), payload.mode);
        if (!result)
        {
            res.status = 400;
            res.set_content(json{{"status", "error"}, {"reason", result.error()}}.dump(), "application/json");
            return;
        }

        if (m_sse_broadcaster)
        {
            m_sse_broadcaster->push_monitoring_status(build_monitoring_status());
        }

        res.set_content(json{{"status", "ok"}}.dump(), "application/json");
    });
}

void IntegratedWebServer::setup_monitoring_config_route()
{
    // PUT /api/monitoring/config — runtime-only tunables (currently
    // cooldown_seconds). Non-persistent: not written back to
    // YAML, so a backend restart returns the YAML default.
    m_server->Put("/api/monitoring/config", [this](const httplib::Request &req, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("PUT /api/monitoring/config");
        if (!is_vlm_ready_or_503(res))
        {
            return;
        }
        auto tracker = m_app->event_state_tracker();
        if (!tracker)
        {
            res.status = 503;
            res.set_content(json{{"error", "vlm_loading"}, {"state", "loading"}, {"detail", ""}}.dump(),
                            "application/json");
            return;
        }

        json body;
        try
        {
            body = json::parse(req.body);
        }
        catch (const std::exception &e)
        {
            res.status = 400;
            res.set_content(json{{"error", "bad_json"}, {"detail", e.what()}}.dump(), "application/json");
            return;
        }

        if (!body.contains("cooldown_seconds") || !body["cooldown_seconds"].is_number_integer())
        {
            res.status = 400;
            res.set_content(json{{"error", "bad_request"}, {"detail", "cooldown_seconds (int) required"}}.dump(),
                            "application/json");
            return;
        }
        const int64_t requested = body["cooldown_seconds"].get<int64_t>();
        constexpr int64_t kMinCooldown = 30;
        constexpr int64_t kMaxCooldown = 300;
        if (requested < kMinCooldown || requested > kMaxCooldown)
        {
            res.status = 400;
            res.set_content(json{{"error", "out_of_range"},
                                 {"detail", "cooldown_seconds must be in [30, 300]"},
                                 {"min", kMinCooldown},
                                 {"max", kMaxCooldown}}
                                .dump(),
                            "application/json");
            return;
        }

        tracker->set_cooldown(std::chrono::seconds(requested));
        HAILO_ANALYTICS_LOG_INFO("EventStateTracker: cooldown set to {} s (runtime, not persisted)", requested);

        if (m_sse_broadcaster && m_app)
        {
            m_sse_broadcaster->push_monitoring_status(m_app->build_monitoring_status());
        }

        res.set_content(json{{"cooldown_seconds", requested}}.dump(), "application/json");
    });
}

void IntegratedWebServer::setup_monitoring_status_route()
{
    m_server->Get("/api/monitoring/status", [this](const httplib::Request & /*req*/, httplib::Response &res) {
        const auto status = build_monitoring_status();
        json response = {
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
        res.set_content(response.dump(), "application/json");
    });
}

void IntegratedWebServer::setup_triggered_events_stream_route()
{
    m_server->Get("/api/triggered-events/stream", [this](const httplib::Request & /*req*/, httplib::Response &res) {
        if (!m_sse_broadcaster)
        {
            res.status = 503;
            res.set_content(json{{"status", "error"}, {"message", "SseBroadcaster not available"}}.dump(),
                            "application/json");
            return;
        }
        m_sse_broadcaster->attach_client(res, build_monitoring_status());
    });
}

void IntegratedWebServer::serve_index_html(httplib::Response &res)
{
    std::ifstream file(vlm_app::paths::webfrontend_index);
    if (file.good())
    {
        std::stringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), "text/html");
        return;
    }

    // Fallback page if index.html is missing on disk.
    static const char *kFallback = R"(<!DOCTYPE html>
<html><head><title>VLM Event Monitor — index.html not installed</title></head>
<body style="font-family:sans-serif;padding:2rem;">
<h1>VLM Event Monitor</h1>
<p>index.html was not found on the device. Expected at:</p>
<pre>/home/root/apps/vlm_event_monitor/resources/webfrontend/index.html</pre>
</body></html>)";
    res.set_content(kFallback, "text/html");
}

// ─── Chat sessions ────────────────────────────────────────────────

namespace
{

constexpr int kBase64InvalidEntry = -1;

std::array<int, 256> make_base64_decode_table()
{
    std::array<int, 256> table{};
    for (auto &entry : table)
    {
        entry = kBase64InvalidEntry;
    }
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t index = 0; index < alphabet.size(); index++)
    {
        table[static_cast<unsigned char>(alphabet[index])] = static_cast<int>(index);
    }
    return table;
}

bool base64_decode(const std::string &input, std::vector<uint8_t> &out)
{
    static const auto kTable = make_base64_decode_table();
    out.clear();
    out.reserve(input.size() * 3 / 4);

    int bits_collected = 0;
    uint32_t accumulator = 0;
    for (char raw : input)
    {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte == '=' || std::isspace(byte))
        {
            continue; // padding / whitespace
        }
        const int value = kTable[byte];
        if (value == kBase64InvalidEntry)
        {
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits_collected += 6;
        if (bits_collected >= 8)
        {
            bits_collected -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bits_collected) & 0xFF));
        }
    }
    return true;
}

} // namespace

void IntegratedWebServer::setup_chat_routes()
{
    // POST /api/vlm/chat/start — body: { frames?: [base64...], live?: bool,
    //                                    triggered_event_id? }
    // Stashes the supplied frames (or grabs latest() for live-chat) and
    // returns a session_id. Auto-evicts any prior session per the
    // single-session model. Inference does NOT fire here.
    m_server->Post("/api/vlm/chat/start", [this](const httplib::Request &req, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("POST /api/vlm/chat/start");
        if (!is_vlm_ready_or_503(res))
        {
            return;
        }
        auto chat_broker = m_app->chat_broker();
        if (!chat_broker)
        {
            // Defensive: vlm_load_state() said Ready but the broker
            // wasn't published yet (extremely tight race; should never
            // happen post-Ready transition). Treat as still loading.
            res.status = 503;
            res.set_content(json{{"error", "vlm_loading"}, {"state", "loading"}, {"detail", ""}}.dump(),
                            "application/json");
            return;
        }

        json body;
        try
        {
            body = json::parse(req.body);
        }
        catch (const std::exception &e)
        {
            res.status = 400;
            res.set_content(json{{"status", "error"}, {"reason", "bad_json"}, {"message", e.what()}}.dump(),
                            "application/json");
            return;
        }

        const bool live = body.value("live", false);

        tl::expected<vlm_event_monitor::ChatSessionBroker::StartResult, std::string> start_result =
            tl::make_unexpected(std::string{});
        if (live)
        {
            start_result = chat_broker->start_live_chat();
        }
        else
        {
            if (!body.contains("frames") || !body["frames"].is_array() || body["frames"].empty())
            {
                res.status = 400;
                res.set_content(json{{"status", "error"},
                                     {"reason", "bad_request"},
                                     {"message", "frames must be a non-empty array unless live=true"}}
                                    .dump(),
                                "application/json");
                return;
            }

            std::vector<std::vector<uint8_t>> jpeg_frames;
            jpeg_frames.reserve(body["frames"].size());
            for (const auto &frame_b64 : body["frames"])
            {
                if (!frame_b64.is_string())
                {
                    res.status = 400;
                    res.set_content(json{{"status", "error"},
                                         {"reason", "bad_request"},
                                         {"message", "each frame must be a base64 string"}}
                                        .dump(),
                                    "application/json");
                    return;
                }
                std::vector<uint8_t> bytes;
                if (!base64_decode(frame_b64.get<std::string>(), bytes) || bytes.empty())
                {
                    res.status = 400;
                    res.set_content(
                        json{{"status", "error"}, {"reason", "bad_request"}, {"message", "base64 decode failed"}}
                            .dump(),
                        "application/json");
                    return;
                }
                jpeg_frames.push_back(std::move(bytes));
            }
            start_result = chat_broker->start_event_chat(jpeg_frames);
        }

        if (!start_result)
        {
            res.status = 500;
            res.set_content(
                json{{"status", "error"}, {"reason", "start_failed"}, {"message", start_result.error()}}.dump(),
                "application/json");
            return;
        }

        json response{
            {"status", "ok"},
            {"session_id", start_result->session_id},
            {"frame_count", start_result->frame_count},
            {"message", "Chat session started. Monitoring continues in background."},
        };
        if (!start_result->snapshot_base64.empty())
        {
            response["snapshot"] = start_result->snapshot_base64;
        }
        res.set_content(response.dump(), "application/json");
    });

    // POST /api/vlm/chat/stream — body: { session_id, prompt, max_generated_tokens? }
    // Returns chunked text/event-stream. Each token arrives as one
    // `data: {"token":"..."}` frame; the final frame is
    // `data: {"done":true,...,"stats":{...}}`.
    m_server->Post("/api/vlm/chat/stream", [this](const httplib::Request &req, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("POST /api/vlm/chat/stream");
        if (!is_vlm_ready_or_503(res))
        {
            return;
        }
        auto chat_broker = m_app->chat_broker();
        if (!chat_broker)
        {
            res.status = 503;
            res.set_content(json{{"error", "vlm_loading"}, {"state", "loading"}, {"detail", ""}}.dump(),
                            "application/json");
            return;
        }

        json body;
        try
        {
            body = json::parse(req.body);
        }
        catch (const std::exception &e)
        {
            res.status = 400;
            res.set_content(json{{"status", "error"}, {"reason", "bad_json"}, {"message", e.what()}}.dump(),
                            "application/json");
            return;
        }
        if (!body.contains("session_id") || !body["session_id"].is_number_integer())
        {
            res.status = 400;
            res.set_content(
                json{{"status", "error"}, {"reason", "bad_request"}, {"message", "session_id (uint32) required"}}
                    .dump(),
                "application/json");
            return;
        }
        const uint32_t session_id = body["session_id"].get<uint32_t>();
        const std::string prompt = body.value("prompt", std::string{});
        if (prompt.empty())
        {
            res.status = 400;
            res.set_content(json{{"status", "error"}, {"reason", "bad_request"}, {"message", "prompt required"}}.dump(),
                            "application/json");
            return;
        }
        const uint32_t max_tokens = body.value("max_generated_tokens", 0u);

        auto handler = std::make_shared<vlm_event_monitor::ChatStreamHandler>();
        auto submit_result = chat_broker->run_stream(session_id, prompt, max_tokens, handler);
        if (!submit_result)
        {
            if (submit_result.error() == "session_not_found")
            {
                res.status = 404;
            }
            else
            {
                res.status = 500;
            }
            res.set_content(json{{"status", "error"}, {"reason", submit_result.error()}}.dump(), "application/json");
            return;
        }

        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider(
            "text/event-stream",
            [handler](size_t /*offset*/, httplib::DataSink &sink) -> bool {
                if (handler->client_disconnected())
                {
                    sink.done();
                    return false;
                }
                std::string frame = handler->next_frame_or_block();
                if (frame.empty())
                {
                    // Periodic wakeup with nothing yet; or already done
                    // and drained. Distinguish via finished().
                    if (handler->finished() || handler->client_disconnected())
                    {
                        sink.done();
                        return false;
                    }
                    return true; // keep the stream open and wait again
                }
                if (!sink.write(frame.data(), frame.size()))
                {
                    handler->mark_client_disconnected();
                    return false;
                }
                return true;
            },
            [handler](bool /*success*/) { handler->mark_client_disconnected(); });
    });

    // POST /api/vlm/chat/close — body: { session_id }
    m_server->Post("/api/vlm/chat/close", [this](const httplib::Request &req, httplib::Response &res) {
        HAILO_ANALYTICS_LOG_INFO("POST /api/vlm/chat/close");
        if (!is_vlm_ready_or_503(res))
        {
            return;
        }
        auto chat_broker = m_app->chat_broker();
        if (!chat_broker)
        {
            res.status = 503;
            res.set_content(json{{"error", "vlm_loading"}, {"state", "loading"}, {"detail", ""}}.dump(),
                            "application/json");
            return;
        }
        json body;
        try
        {
            body = json::parse(req.body);
        }
        catch (const std::exception &e)
        {
            res.status = 400;
            res.set_content(json{{"status", "error"}, {"reason", "bad_json"}, {"message", e.what()}}.dump(),
                            "application/json");
            return;
        }
        if (!body.contains("session_id") || !body["session_id"].is_number_integer())
        {
            res.status = 400;
            res.set_content(
                json{{"status", "error"}, {"reason", "bad_request"}, {"message", "session_id (uint32) required"}}
                    .dump(),
                "application/json");
            return;
        }
        const uint32_t session_id = body["session_id"].get<uint32_t>();
        auto close_result = chat_broker->close_session(session_id);
        if (!close_result)
        {
            res.status = (close_result.error() == "session_not_found") ? 404 : 500;
            res.set_content(json{{"status", "error"}, {"reason", close_result.error()}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"status", "ok"}}.dump(), "application/json");
    });
}
