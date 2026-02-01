#pragma once
#include "common/resources.hpp"
#include "rtc/rtc.hpp"
#include "encoder.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"

using namespace hailo_analytics::pipeline::sinks;

namespace webserver
{
namespace resources
{
class WebRtcResource : public Resource, public RTPConverterStage::RTPReceiver
{
  private:
    struct WebrtcSession
    {
        rtp_session_id_t session_id;
        std::shared_ptr<rtc::PeerConnection> peer_connection;
        std::shared_ptr<rtc::Track> track;
        rtc::PeerConnection::State state;
        rtc::PeerConnection::GatheringState gathering_state;
        rtc::SSRC ssrc;
        std::string codec;
        nlohmann::json ICE_offer;
    };
    std::string m_stream_codec;
    std::shared_mutex m_session_mutex;
    const std::map<std::string, int> codec_payload_type_map = {
        {"CODEC_TYPE_H264", 96},
        {"CODEC_TYPE_HEVC", 98},
    };
    std::shared_ptr<WebrtcSession> create_media_sender(rtp_session_id_t session_id);
    std::map<std::string, rtp_session_id_t> m_supported_sessions;
    std::map<rtp_session_id_t, std::shared_ptr<WebrtcSession>> m_sessions;

  public:
    WebRtcResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceBase> configs);
    void on_rtp_packet(GstSample *sample, rtp_session_id_t session_id) override;
    rtp_session_id_t start(std::string session_name) override;
    void stop(rtp_session_id_t session_id) override;
    void remove_inactive_sessions();
    void close_all_connections();
    void http_register(std::shared_ptr<HTTPServer> srv) override;
    std::string name() override
    {
        return "webrtc";
    }
    ResourceType get_type() override
    {
        return ResourceType::RESOURCE_WEBRTC;
    }
};
} // namespace resources
} // namespace webserver
