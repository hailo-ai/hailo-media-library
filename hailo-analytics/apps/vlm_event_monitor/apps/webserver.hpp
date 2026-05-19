#pragma once

#include <memory>
#include <string>

#include <tl/expected.hpp>

#include "utils/vlm_app_config.hpp"

namespace httplib
{
class Server;
class Response;
} // namespace httplib

namespace vlm_event_monitor
{
class ChatSessionBroker;
class EventStore;
class SseBroadcaster;
struct MonitoringStatus;
} // namespace vlm_event_monitor

class VlmEventMonitorPipeline;
class WebRTCStreamerExt;

class IntegratedWebServer
{
  public:
    IntegratedWebServer(const IntegratedWebServer &) = delete;
    IntegratedWebServer &operator=(const IntegratedWebServer &) = delete;
    IntegratedWebServer(IntegratedWebServer &&) noexcept = default;
    IntegratedWebServer &operator=(IntegratedWebServer &&) noexcept = default;

    static tl::expected<std::shared_ptr<IntegratedWebServer>, std::string> create(
        const vlm_app_config::VlmAppConfig &config);

    void start(std::string host, int port = 80);
    void stop();

  private:
    explicit IntegratedWebServer(const vlm_app_config::VlmAppConfig &config);

    void setup_cors();
    void setup_routes();
    void setup_root_route();
    void setup_status_route();
    void setup_webrtc_offer_route();
    void setup_webrtc_answer_route();
    void setup_webrtc_ice_candidate_route();
    void setup_webrtc_status_route();
    void setup_webrtc_options_route();
    void setup_events_routes();
    void setup_monitoring_status_route();
    void setup_monitoring_config_route();
    void setup_triggered_events_stream_route();
    void setup_chat_routes();

    // Build a MonitoringStatus snapshot from the current pipeline state.
    // Used both as the SSE greeting payload and for /api/monitoring/status.
    vlm_event_monitor::MonitoringStatus build_monitoring_status() const;

    bool is_vlm_ready_or_503(httplib::Response &res) const;

    void serve_index_html(httplib::Response &res);

    std::unique_ptr<httplib::Server> m_server;
    vlm_app_config::VlmAppConfig m_config;
    std::shared_ptr<VlmEventMonitorPipeline> m_app;
    std::shared_ptr<WebRTCStreamerExt> m_webrtc_streamer_ext;
    std::shared_ptr<vlm_event_monitor::EventStore> m_event_store;
    std::shared_ptr<vlm_event_monitor::SseBroadcaster> m_sse_broadcaster;
};
