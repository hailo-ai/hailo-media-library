#include "webrtc.hpp"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <media_library/frontend.hpp>
#include <rtc/rtc.hpp>
#include <random>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "webrtc_turn.hpp"
#include "common/logger_macros.hpp"
#include "resources/common/events_utils.hpp"
#include "resources/common/resources.hpp"

using namespace webserver::resources;

// WebrtcSession definition (moved here from webrtc.hpp to avoid rtc/rtc.hpp in the header)
struct WebRtcResource::WebrtcSession
{
    rtp_session_id_t session_id;
    std::string stream_name;
    std::shared_ptr<rtc::PeerConnection> peer_connection;
    std::shared_ptr<rtc::Track> track;
    rtc::PeerConnection::State state = rtc::PeerConnection::State::New;
    rtc::PeerConnection::GatheringState gathering_state = rtc::PeerConnection::GatheringState::New;
    rtc::SSRC ssrc;
    std::string codec;
    nlohmann::json ICE_offer;
};

WebRtcResource::~WebRtcResource() = default;

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

std::string WebRtcResource::name()
{
    return "webrtc";
}

ResourceType WebRtcResource::get_type()
{
    return ResourceType::RESOURCE_WEBRTC;
}

WebRtcResource::WebRtcResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceBase> configs)
    : Resource(event_bus)
{
    subscribe_callback(
        EventType::PIPELINE_READY, EventPriority::EVENT_PRIORITY_HIGH,
        [this, configs](ResourceStateChangeNotification /*notification*/) {
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

nlohmann::json WebRtcResource::build_sessions_response_locked() const
{
    nlohmann::json result;

    for (const auto &[stream_name, session_ids] : m_stream_sessions)
    {
        if (!session_ids.empty())
        {
            result[stream_name] = session_ids;
        }
    }

    if (!result.contains("main") || result["main"].empty())
        result["main"] = {"main_init_" + generate_session_id()};
    if (!result.contains("thumbnail") || result["thumbnail"].empty())
        result["thumbnail"] = {"thumbnail_init_" + generate_session_id()};
    if (!result.contains("clip") || result["clip"].empty())
        result["clip"] = {"clip_init_" + generate_session_id()};

    return result;
}

void WebRtcResource::remove_inactive_sessions()
{
    // Use try_lock to avoid potential deadlock if called from within track->send()
    // which holds shared_lock in on_rtp_packet
    std::unique_lock lock(m_session_mutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        WEBSERVER_LOG_INFO("Could not acquire lock for session cleanup, will retry later");
        return;
    }

    WEBSERVER_LOG_INFO("Removing inactive sessions - checking {} sessions", m_sessions.size());

    // Use erase-if pattern for maps (C++20 has std::erase_if, but doing it manually for C++17)
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        auto session = it->second;
        WEBSERVER_LOG_INFO("Checking session {} (stream: {}), state: {}", session->session_id, session->stream_name,
                           rtc_state_to_string(session->state));

        if (session->state == rtc::PeerConnection::State::Closed ||
            session->state == rtc::PeerConnection::State::Failed ||
            session->state == rtc::PeerConnection::State::Disconnected)
        {
            WEBSERVER_LOG_INFO("Removing inactive session {} (stream: {})", session->session_id, session->stream_name);

            // Remove from m_stream_sessions as well
            const std::string &stream_name = session->stream_name;
            auto stream_it = m_stream_sessions.find(stream_name);
            if (stream_it != m_stream_sessions.end())
            {
                auto &ids = stream_it->second;
                ids.erase(std::remove(ids.begin(), ids.end(), session->session_id), ids.end());
                if (ids.empty())
                {
                    m_stream_sessions.erase(stream_it);
                }
            }

            it = m_sessions.erase(it);
        }
        else
        {
            WEBSERVER_LOG_INFO("Keeping active session {} (stream: {})", session->session_id, session->stream_name);
            ++it;
        }
    }
    WEBSERVER_LOG_INFO("After cleanup: {} sessions remaining", m_sessions.size());
}

rtp_session_id_t WebRtcResource::start(std::string stream_name)
{
    // Return stream_name directly - sessions are created via /Offer_RTC HTTP endpoint
    // The RTPConverterStage will use this to broadcast to all sessions for this stream
    WEBSERVER_LOG_INFO("WebRtcResource: start called for stream_name: {}", stream_name);
    return stream_name;
}

void WebRtcResource::stop(std::string stream_name)
{
    WEBSERVER_LOG_INFO("Stopping all sessions for stream: {}", stream_name);
    std::unique_lock lock(m_session_mutex);

    auto stream_it = m_stream_sessions.find(stream_name);
    if (stream_it != m_stream_sessions.end())
    {
        for (const auto &session_id : stream_it->second)
        {
            auto session_it = m_sessions.find(session_id);
            if (session_it != m_sessions.end())
            {
                if (session_it->second->peer_connection->state() != rtc::PeerConnection::State::Closed)
                {
                    session_it->second->peer_connection->close();
                }
                m_sessions.erase(session_it);
            }
        }
        m_stream_sessions.erase(stream_it);
    }
}

void WebRtcResource::stop_session(rtp_session_id_t session_id)
{
    WEBSERVER_LOG_INFO("Stopping session: {}", session_id);
    std::unique_lock lock(m_session_mutex);

    auto session_it = m_sessions.find(session_id);
    if (session_it == m_sessions.end())
    {
        WEBSERVER_LOG_DEBUG("Session {} not found in m_sessions, cannot stop", session_id);
        return;
    }

    const std::string &stream_name = session_it->second->stream_name;
    WEBSERVER_LOG_INFO("Found session {} for stream {}, closing...", session_id, stream_name);

    if (session_it->second->peer_connection->state() != rtc::PeerConnection::State::Closed)
    {
        session_it->second->peer_connection->close();
    }

    auto stream_it = m_stream_sessions.find(stream_name);
    if (stream_it != m_stream_sessions.end())
    {
        auto &ids = stream_it->second;
        ids.erase(std::remove(ids.begin(), ids.end(), session_id), ids.end());
        if (ids.empty())
        {
            m_stream_sessions.erase(stream_it);
        }
    }

    m_sessions.erase(session_it);
    WEBSERVER_LOG_INFO("Session {} stopped successfully", session_id);
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

std::shared_ptr<WebRtcResource::WebrtcSession> WebRtcResource::create_media_sender(rtp_session_id_t session_id,
                                                                                   const std::string &stream_name,
                                                                                   const turn::TurnConfig &turn_config)
{
    WEBSERVER_LOG_INFO("Creating media sender for session {} (stream: {})", session_id, stream_name);
    auto session = std::make_shared<WebrtcSession>();
    session->session_id = session_id;
    session->stream_name = stream_name;
    session->ssrc = 42;
    session->codec = m_stream_codec;

    // Configure ICE servers (including TURN if specified)
    rtc::Configuration config;
    turn::configure_ice_servers(config, turn_config);

    try
    {
        config.bindAddress = get_interface_ip("eth0");
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_WARNING("Could not bind to eth0 ({}), using default interface", e.what());
    }

    session->peer_connection = std::make_shared<rtc::PeerConnection>(config);
    session->peer_connection->onStateChange([this, session](rtc::PeerConnection::State state) {
        WEBSERVER_LOG_INFO("WebRtc State change for session {} (stream: {}): {}", session->session_id,
                           session->stream_name, rtc_state_to_string(state));
        session->state = state;
        if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Disconnected)
        {
            WEBSERVER_LOG_INFO("Session {} disconnected, triggering cleanup", session->session_id);
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

void WebRtcResource::on_rtp_packet(GstSample *sample, rtp_session_id_t stream_name)
{
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        WEBSERVER_LOG_ERROR("Failed to map buffer");
        throw std::runtime_error("Failed to map buffer");
    }

    auto len = gst_buffer_get_size(buffer);
    if (len < sizeof(rtc::RtpHeader))
    {
        gst_buffer_unmap(buffer, &map);
        WEBSERVER_LOG_ERROR("Invalid buffer size");
        return;
    }

    std::shared_lock lock(m_session_mutex);

    if (m_sessions.empty())
    {
        gst_buffer_unmap(buffer, &map);
        return; // No sessions at all
    }

    // Iterate through all connected sessions
    for (const auto &[session_id, session] : m_sessions)
    {
        // Skip sessions that are not ready
        if (session->state != rtc::PeerConnection::State::Connected || !session->track || !session->track->isOpen())
        {
            continue;
        }

        // Check if session stream matches packet stream (main vs thumbnail)
        bool packet_is_thumbnail = (stream_name == "thumbnail");
        bool session_is_thumbnail = (session->stream_name == "thumbnail");
        if (packet_is_thumbnail != session_is_thumbnail)
        {
            continue;
        }

        // Send RTP packet to this session
        try
        {
            auto rtp = reinterpret_cast<rtc::RtpHeader *>(map.data);
            rtp->setSsrc(session->ssrc);
            session->track->send(reinterpret_cast<const std::byte *>(map.data), len);
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_WARNING("Failed to send RTP packet to session {}: {}", session_id, e.what());
        }
    }

    gst_buffer_unmap(buffer, &map);
}

void WebRtcResource::http_register(HTTPServer &srv)
{
    WEBSERVER_LOG_INFO("WebRtcResource::http_register Registering HTTP endpoints");
    srv.Get("/webrtc_sessions", std::function<nlohmann::json()>([this]() {
                WEBSERVER_LOG_DEBUG("GET /webrtc_sessions called");
                nlohmann::json j;
                {
                    std::shared_lock lock(m_session_mutex);
                    j = build_sessions_response_locked();
                }
                WEBSERVER_LOG_DEBUG("GET /webrtc_sessions completed");
                return j;
            }));

    srv.Delete("/webrtc_sessions",
               std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                   WEBSERVER_LOG_INFO("DELETE /webrtc_sessions called with: {}", j_body.dump());
                   try
                   {
                       if (!j_body.contains("session_id"))
                       {
                           return nlohmann::json{{"error", "Missing session_id"}};
                       }

                       rtp_session_id_t session_id = j_body["session_id"];

                       // Check if this is a "main" stream session - protect it from deletion
                       // This allows parallel streaming (main + external popup)
                       {
                           std::shared_lock lock(m_session_mutex);
                           auto session_it = m_sessions.find(session_id);
                           if (session_it != m_sessions.end())
                           {
                               const std::string &stream_name = session_it->second->stream_name;
                               if (stream_name == "main")
                               {
                                   WEBSERVER_LOG_INFO("Ignoring DELETE request for main stream session {} - "
                                                      "main sessions are protected to allow parallel streaming",
                                                      session_id);
                                   // Return current sessions without deleting
                                   return build_sessions_response_locked();
                               }
                           }
                       }

                       stop_session(session_id);

                       nlohmann::json result;
                       {
                           std::shared_lock lock(m_session_mutex);
                           result = build_sessions_response_locked();
                       }
                       WEBSERVER_LOG_DEBUG("DELETE /webrtc_sessions completed");
                       return result;
                   }
                   catch (const std::exception &e)
                   {
                       WEBSERVER_LOG_ERROR("Failed to delete session: {}", e.what());
                       return nlohmann::json{{"error", e.what()}};
                   }
               }));

    srv.Post("/Offer_RTC", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                 WEBSERVER_LOG_DEBUG("Creating new WebRTC connection");
                 try
                 {
                     // Load TURN configuration once for this request
                     turn::TurnConfig turn_config = turn::load_turn_config_from_env();

                     std::string stream_name = j_body.value("stream_name", "main");
                     rtp_session_id_t session_id = generate_session_id();
                     std::shared_ptr<WebRtcResource::WebrtcSession> session =
                         this->create_media_sender(session_id, stream_name, turn_config);

                     while (session->gathering_state != rtc::PeerConnection::GatheringState::Complete ||
                            session->ICE_offer.is_null())
                     {
                         std::this_thread::sleep_for(std::chrono::milliseconds(1));
                     }

                     {
                         std::unique_lock lock(m_session_mutex);
                         m_sessions[session_id] = session;
                         m_stream_sessions[stream_name].push_back(session_id);
                     }

                     nlohmann::json ret = {{"session_id", session_id},
                                           {"stream_name", stream_name},
                                           {"rtc_status", gathering_state_to_string(session->gathering_state)},
                                           {"rtc_offer", session->ICE_offer}};

                     // Include ICE server configuration for client
                     ret["ice_servers"] = turn::build_ice_servers_json(turn_config);

                     WEBSERVER_LOG_DEBUG("WebRTC connection created successfully with ID {} for stream {}", session_id,
                                         stream_name);
                     return ret;
                 }
                 catch (const std::exception &e)
                 {
                     WEBSERVER_LOG_ERROR("Failed to create WebRTC offer: {}", e.what());
                     return nlohmann::json{{"error", e.what()}};
                 }
             }));

    srv.Post("/Response_RTC", std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                 WEBSERVER_LOG_INFO("Processing WebRTC response: {}", j_body.dump());
                 try
                 {
                     // Validate required fields exist and are correct types
                     if (!j_body.contains("sdp") || !j_body.contains("type") || !j_body.contains("session_id"))
                     {
                         WEBSERVER_LOG_ERROR("Missing required fields in WebRTC response. Received: {}", j_body.dump());
                         throw std::runtime_error("Missing required fields: sdp, type, or session_id");
                     }

                     // Handle sdp field - could be string or array (take first element if array)
                     std::string sdp_str;
                     if (j_body["sdp"].is_array())
                     {
                         if (j_body["sdp"].empty())
                         {
                             throw std::runtime_error("sdp array is empty");
                         }
                         sdp_str = j_body["sdp"][0].get<std::string>();
                     }
                     else
                     {
                         sdp_str = j_body["sdp"].get<std::string>();
                     }

                     // Handle type field - could be string or array
                     std::string type_str;
                     if (j_body["type"].is_array())
                     {
                         if (j_body["type"].empty())
                         {
                             throw std::runtime_error("type array is empty");
                         }
                         type_str = j_body["type"][0].get<std::string>();
                     }
                     else
                     {
                         type_str = j_body["type"].get<std::string>();
                     }

                     // Handle session_id field - could be string or array
                     rtp_session_id_t session_id;
                     if (j_body["session_id"].is_array())
                     {
                         if (j_body["session_id"].empty())
                         {
                             WEBSERVER_LOG_ERROR("session_id is empty array. Full request: {}", j_body.dump());
                             throw std::runtime_error("session_id array is empty");
                         }
                         session_id = j_body["session_id"][0].get<std::string>();
                     }
                     else
                     {
                         session_id = j_body["session_id"].get<std::string>();
                     }

                     rtc::Description answer(sdp_str, type_str);
                     std::shared_lock lock(m_session_mutex);
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
