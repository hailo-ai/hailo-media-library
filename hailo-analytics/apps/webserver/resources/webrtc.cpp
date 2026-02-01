#include "webrtc.hpp"
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace webserver::resources;

// Helper function to generate session id
inline rtp_session_id_t generate_session_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    rtp_session_id_t session_id;
    for (int i = 0; i < 16; ++i)
    {
        session_id += "0123456789abcdef"[dis(gen)];
    }
    return session_id;
}

WebRtcResource::WebRtcResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceBase> configs)
    : Resource(event_bus)
{
    subscribe_callback(
        EventType::PIPELINE_READY, EventPriority::EVENT_PRIORITY_HIGH,
        [this, configs](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("Initializing WebRtcResource");
            this->m_stream_codec =
                configs->get_encoder_default_config()["hailo_encoder"]["config"]["output_stream"]["codec"];
        });
}

std::string get_interface_ip(const std::string &iface)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        throw std::runtime_error("socket() failed");

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0)
    {
        close(fd);
        throw std::runtime_error("ioctl(SIOCGIFADDR) failed");
    }
    close(fd);

    struct sockaddr_in *ipaddr = reinterpret_cast<struct sockaddr_in *>(&ifr.ifr_addr);
    return inet_ntoa(ipaddr->sin_addr);
}

std::string gathering_state_to_string(rtc::PeerConnection::GatheringState state)
{
    switch (state)
    {
    case rtc::PeerConnection::GatheringState::New:
        return "NEW";
    case rtc::PeerConnection::GatheringState::InProgress:
        return "INPROGRESS";
    case rtc::PeerConnection::GatheringState::Complete:
        return "COMPLETE";
    default:
        std::runtime_error("Unknown gathering state");
        return "Unknown";
    }
}

std::string rtc_state_to_string(rtc::PeerConnection::State state)
{
    switch (state)
    {
    case rtc::PeerConnection::State::New:
        return "NEW";
    case rtc::PeerConnection::State::Connecting:
        return "CONNECTING";
    case rtc::PeerConnection::State::Connected:
        return "CONNECTED";
    case rtc::PeerConnection::State::Disconnected:
        return "DISCONNECTED";
    case rtc::PeerConnection::State::Failed:
        return "FAILED";
    case rtc::PeerConnection::State::Closed:
        return "CLOSED";
    default:
        std::runtime_error("Unknown rtc state");
        return "Unknown";
    }
}

void WebRtcResource::remove_inactive_sessions()
{
    WEBSERVER_LOG_INFO("Removing inactive sessions");
    std::unique_lock lock(m_session_mutex);

    // Use erase-if pattern for maps (C++20 has std::erase_if, but doing it manually for C++17)
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        auto session = it->second;
        if (session->state == rtc::PeerConnection::State::Closed ||
            session->state == rtc::PeerConnection::State::Failed ||
            session->state == rtc::PeerConnection::State::Disconnected)
        {
            it = m_sessions.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

rtp_session_id_t WebRtcResource::start(std::string session_name)
{
    rtp_session_id_t session_id = generate_session_id();
    m_supported_sessions[session_name] = session_id;
    WEBSERVER_LOG_INFO("WebRtcResource: start called, generated session_id {} for session_name: {}", session_id,
                       session_name);
    return session_id;
}

void WebRtcResource::stop(rtp_session_id_t session_id)
{
    std::unique_lock lock(m_session_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        if (it->second->peer_connection->state() != rtc::PeerConnection::State::Closed)
        {
            it->second->peer_connection->close();
        }
        m_sessions.erase(it);
    }

    // Remove from supported sessions
    for (auto it2 = m_supported_sessions.begin(); it2 != m_supported_sessions.end(); ++it2)
    {
        if (it2->second == session_id)
        {
            m_supported_sessions.erase(it2);
            break;
        }
    }
}

void WebRtcResource::close_all_connections()
{
    WEBSERVER_LOG_INFO("Closing all WebRTC connections...");

    for (auto &session : m_sessions)
    {
        if (session.second->peer_connection->state() != rtc::PeerConnection::State::Closed)
        {
            session.second->peer_connection->close();
        }
    }
    WEBSERVER_LOG_INFO("All WebRTC sessions closed.");
}

std::shared_ptr<WebRtcResource::WebrtcSession> WebRtcResource::create_media_sender(rtp_session_id_t session_id)
{
    WEBSERVER_LOG_INFO("Creating media sender");
    auto session = std::make_shared<WebrtcSession>();
    session->session_id = session_id;
    session->ssrc = 42;
    session->codec = m_stream_codec;
    // Ensure no external ICE servers are provided
    rtc::Configuration config;
    config.iceServers.clear();
    config.bindAddress = get_interface_ip("eth0");
    session->peer_connection = std::make_shared<rtc::PeerConnection>(config);
    session->peer_connection->onStateChange([this, session](rtc::PeerConnection::State state) {
        WEBSERVER_LOG_INFO("WebRtc State: {}", rtc_state_to_string(state));
        session->state = state;
        if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Disconnected)
        {
            remove_inactive_sessions();
        }
    });

    session->peer_connection->onGatheringStateChange([this, session](rtc::PeerConnection::GatheringState state) {
        WEBSERVER_LOG_INFO("WebRtc Gathering State: {}", gathering_state_to_string(state));
        session->gathering_state = state;
        if (state == rtc::PeerConnection::GatheringState::Complete)
        {
            auto description = session->peer_connection->localDescription();
            nlohmann::json message = {{"type", description->typeString()}, {"sdp", std::string(description.value())}};
            session->ICE_offer = message;
            WEBSERVER_LOG_DEBUG("Generated ICE offer: {}", message.dump());
        }
    });
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    if (session->codec == "CODEC_TYPE_H264")
        media.addH264Codec(this->codec_payload_type_map.at(session->codec));
    else if (session->codec == "CODEC_TYPE_HEVC")
        media.addH265Codec(this->codec_payload_type_map.at(session->codec));
    else
        throw std::runtime_error("Unsupported codec");
    media.addSSRC(session->ssrc, "video-send");
    session->track = session->peer_connection->addTrack(media);
    session->peer_connection->setLocalDescription();

    return session;
}

void WebRtcResource::on_rtp_packet(GstSample *sample, rtp_session_id_t session_id)
{
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        WEBSERVER_LOG_ERROR("Failed to map buffer");
        throw std::runtime_error("Failed to map buffer");
    }
    std::shared_lock lock(m_session_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        auto &session = it->second;
        if (session->state != rtc::PeerConnection::State::Connected)
        {
            gst_buffer_unmap(buffer, &map);
            return;
        }
        if (!session->track->isOpen())
        {
            WEBSERVER_LOG_WARNING("Track is not open yet. Cannot send RTP packet.");
            gst_buffer_unmap(buffer, &map);
            return;
        }
        auto len = gst_buffer_get_size(buffer);
        if (len < sizeof(rtc::RtpHeader) || !session->track->isOpen())
        {
            gst_buffer_unmap(buffer, &map);
            WEBSERVER_LOG_ERROR("Invalid buffer size or track not open");
            throw std::runtime_error("Invalid buffer size or track not open");
        }
        auto rtp = reinterpret_cast<rtc::RtpHeader *>(map.data);
        rtp->setSsrc(session->ssrc);
        session->track->send(reinterpret_cast<const std::byte *>(map.data), len);
    }
    gst_buffer_unmap(buffer, &map);
}

void WebRtcResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    WEBSERVER_LOG_INFO("WebRtcResource::http_register Registering HTTP endpoints");
    srv->Get("/webrtc_sessions", std::function<nlohmann::json()>([this]() {
                 WEBSERVER_LOG_DEBUG("GET /webrtc_sessions called");
                 nlohmann::json j;
                 {
                     std::shared_lock lock(m_session_mutex);
                     j = m_supported_sessions;
                 }
                 WEBSERVER_LOG_DEBUG("GET /webrtc_sessions completed");
                 return j;
             }));
    srv->Post("/Offer_RTC", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  WEBSERVER_LOG_DEBUG("Creating new WebRTC connection");
                  try
                  {
                      rtp_session_id_t session_id = j_body["session_id"];
                      std::shared_ptr<WebRtcResource::WebrtcSession> session = this->create_media_sender(session_id);
                      std::unique_lock lock(m_session_mutex);

                      while (session->gathering_state != rtc::PeerConnection::GatheringState::Complete)
                      {
                          std::this_thread::sleep_for(std::chrono::milliseconds(1));
                      }

                      nlohmann::json ret = {{"rtc_status", gathering_state_to_string(session->gathering_state)},
                                            {"rtc_offer", session->ICE_offer}};

                      m_sessions[session_id] = session;
                      WEBSERVER_LOG_DEBUG("WebRTC connection created successfully with ID {}", session_id);
                      return ret;
                  }
                  catch (const std::exception &e)
                  {
                      WEBSERVER_LOG_ERROR("Failed to create WebRTC offer: {}", e.what());
                      return nlohmann::json{{"error", "Failed to create WebRTC offer"}};
                  }
              }));

    srv->Post("/Response_RTC", std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  WEBSERVER_LOG_DEBUG("Processing WebRTC response");
                  try
                  {
                      rtc::Description answer(j_body["sdp"].get<std::string>(), j_body["type"].get<std::string>());
                      std::shared_lock lock(m_session_mutex);
                      rtp_session_id_t session_id = j_body["session_id"];
                      auto session = m_sessions[session_id];
                      if (session)
                      {
                          session->peer_connection->setRemoteDescription(answer);
                          WEBSERVER_LOG_DEBUG("Remote description set successfully for session {}", session_id);
                      }
                      else
                      {
                          WEBSERVER_LOG_WARNING("No active session found with id {} to set remote description",
                                                session_id);
                      }
                  }
                  catch (const std::exception &e)
                  {
                      WEBSERVER_LOG_ERROR("Error processing WebRTC response: {}", e.what());
                      throw;
                  }
                  WEBSERVER_LOG_DEBUG("WebRTC response processed successfully");
              }));
}
