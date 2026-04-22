#include "hailo_analytics/pipeline/sinks/websocket_sink_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline;

WebSocketSinkStage::WebSocketSinkStage(const std::string &name, uint16_t port, const std::string &host,
                                       size_t queue_size, bool leaky, size_t max_message_size)
    : ThreadedStage(name, queue_size, leaky, false), m_port(port), m_host(host), m_max_message_size(max_message_size)
{
}

WebSocketSinkStage::~WebSocketSinkStage() = default;

AppStatus WebSocketSinkStage::init()
{
    rtc::WebSocketServer::Configuration config;
    config.port = m_port;
    config.bindAddress = m_host;
    config.maxMessageSize = m_max_message_size;
    m_ws_server = std::make_unique<rtc::WebSocketServer>(config);

    m_ws_server->onClient([this](std::shared_ptr<rtc::WebSocket> client_ws) {
        int client_id;
        {
            std::unique_lock lock(m_clients_mutex);
            client_id = m_next_client_id++;
            m_clients[client_id] = client_ws;
        }
        HAILO_ANALYTICS_LOG_INFO("WebSocket sink client {} connected", client_id);

        client_ws->onClosed([this, client_id]() {
            std::unique_lock lock(m_clients_mutex);
            m_clients.erase(client_id);
            HAILO_ANALYTICS_LOG_INFO("WebSocket sink client {} disconnected", client_id);
        });

        client_ws->onError([this, client_id](std::string error) {
            std::unique_lock lock(m_clients_mutex);
            m_clients.erase(client_id);
            HAILO_ANALYTICS_LOG_WARN("WebSocket sink client {} error: {}", client_id, error);
        });
    });

    HAILO_ANALYTICS_LOG_INFO("WebSocket server started on {}:{}", m_host, m_port);
    return AppStatus::SUCCESS;
}

AppStatus WebSocketSinkStage::deinit()
{
    {
        std::unique_lock lock(m_clients_mutex);
        for (auto &[id, ws] : m_clients)
        {
            ws->onClosed(nullptr);
            ws->onError(nullptr);
            ws->close();
        }
        m_clients.clear();
    }

    if (m_ws_server)
        m_ws_server->stop();
    m_ws_server.reset();

    return AppStatus::SUCCESS;
}

AppStatus WebSocketSinkStage::process(BufferPtr data)
{
    for (const auto &obj : data->get_roi()->get_objects())
    {
        if (obj->get_type() != HAILO_ZMQ)
            continue;
        auto zmq_msg = std::static_pointer_cast<HailoZMQMessage>(obj);
        if (zmq_msg->has_output_msg())
        {
            broadcast(zmq_msg->get_output_msg());
            break;
        }
    }

    return AppStatus::SUCCESS;
}

void WebSocketSinkStage::broadcast(const std::string &json_str)
{
    std::vector<std::pair<int, std::shared_ptr<rtc::WebSocket>>> snapshot;
    {
        std::shared_lock lock(m_clients_mutex);
        snapshot.assign(m_clients.begin(), m_clients.end());
    }
    HAILO_ANALYTICS_LOG_TRACE("WebSocket sink broadcast to {} client(s), {} bytes", snapshot.size(), json_str.size());
    for (auto &[id, client_ws] : snapshot)
    {
        try
        {
            client_ws->send(json_str);
        }
        catch (const std::exception &e)
        {
            HAILO_ANALYTICS_LOG_WARN("Failed to send to WebSocket sink client {}: {}", id, e.what());
        }
    }
}

WebSocketSinkStageBuild::Builder &WebSocketSinkStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = std::move(name);
    return *this;
}

WebSocketSinkStageBuild::Builder &WebSocketSinkStageBuild::Builder::set_port_opt(uint16_t port)
{
    m_port = port;
    return *this;
}

WebSocketSinkStageBuild::Builder &WebSocketSinkStageBuild::Builder::set_host_opt(std::string host)
{
    m_host = std::move(host);
    return *this;
}

WebSocketSinkStageBuild::Builder &WebSocketSinkStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

WebSocketSinkStageBuild::Builder &WebSocketSinkStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

WebSocketSinkStageBuild::Builder &WebSocketSinkStageBuild::Builder::set_max_message_size_opt(size_t size)
{
    m_max_message_size = size;
    return *this;
}

std::shared_ptr<WebSocketSinkStage> WebSocketSinkStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    return std::make_shared<WebSocketSinkStage>(m_stage_name.value(), m_port, m_host, m_queue_size, m_leaky,
                                                m_max_message_size);
}

WebSocketSinkStageBuild::Builder WebSocketSinkStageBuild::create()
{
    return Builder();
}
