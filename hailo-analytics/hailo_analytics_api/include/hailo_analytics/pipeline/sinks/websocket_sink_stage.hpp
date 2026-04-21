#pragma once

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "rtc/rtc.hpp"
#include <shared_mutex>
#include <unordered_map>

#define WEBSOCKET_SINK_QUEUE_SIZE_DEFAULT (1)
#define WEBSOCKET_SINK_PORT_DEFAULT (8765)

static constexpr size_t WEBSOCKET_MAX_MESSAGE_SIZE_DEFAULT = 1024 * 1024; // 1 MB

namespace hailo_analytics::pipeline::sinks
{

class WebSocketSinkStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    WebSocketSinkStage(const std::string &name, uint16_t port = WEBSOCKET_SINK_PORT_DEFAULT,
                       const std::string &host = "0.0.0.0", size_t queue_size = WEBSOCKET_SINK_QUEUE_SIZE_DEFAULT,
                       bool leaky = true, size_t max_message_size = WEBSOCKET_MAX_MESSAGE_SIZE_DEFAULT);
    ~WebSocketSinkStage();

    AppStatus init() override;
    AppStatus process(BufferPtr data) override;
    AppStatus deinit() override;

  private:
    void broadcast(const std::string &json_str);

    uint16_t m_port;
    std::string m_host;
    size_t m_max_message_size;
    std::unique_ptr<rtc::WebSocketServer> m_ws_server;
    std::shared_mutex m_clients_mutex;
    std::unordered_map<int, std::shared_ptr<rtc::WebSocket>> m_clients;
    int m_next_client_id{0};
};

class WebSocketSinkStageBuild : public WebSocketSinkStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        uint16_t m_port = WEBSOCKET_SINK_PORT_DEFAULT;
        std::string m_host = "0.0.0.0";
        size_t m_queue_size = WEBSOCKET_SINK_QUEUE_SIZE_DEFAULT;
        bool m_leaky = true;
        size_t m_max_message_size = WEBSOCKET_MAX_MESSAGE_SIZE_DEFAULT;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_port_opt(uint16_t port);
        Builder &set_host_opt(std::string host);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_max_message_size_opt(size_t size);
        std::shared_ptr<WebSocketSinkStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
