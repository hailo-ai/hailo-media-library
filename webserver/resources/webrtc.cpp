#include "resources.hpp"
using namespace webserver::resources;

WebRtcResource::WebRtcResource()
    : ssrc(42),
      codec("H264")
      {
        // Ensure no external ICE servers are provided
        rtc::Configuration config;
        config.iceServers.clear();
        config.bindAddress = "10.0.0.1";
        this->peer_connection = std::make_shared<rtc::PeerConnection>(config);
        
        this->create_media_sender();
        peer_connection->setLocalDescription();
        this->state = rtc::PeerConnection::State::New;

      }

void WebRtcResource::setCodec(std::string codec_type) {
    if (codec_payload_type_map.find(codec_type) == codec_payload_type_map.end()) {
        throw std::runtime_error("Codec not supported");
    }
    codec = codec_type;
}

void WebRtcResource::create_media_sender() {
    this->peer_connection->onStateChange(
		    [this](rtc::PeerConnection::State state) { 
                std::cout << "WebRtc State: " << state << std::endl;
                this->state = state;});


    this->peer_connection->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        std::cout << "WebRtc Gathering State: " << state << std::endl;
        this->gathering_state = state;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = this->peer_connection->localDescription();
            nlohmann::json message = {
                            {"type", description->typeString()},
                            {"sdp", std::string(description.value())}};
            this->m_config = message;
        }
    });
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(this->codec_payload_type_map.at(this->codec));
    media.addSSRC(this->ssrc, "video-send");
    this->track = peer_connection->addTrack(media);
}

void WebRtcResource::send_rtp_packet(GstSample *sample){
    if (!track->isOpen()) {
        std::runtime_error("Track not open");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        std::runtime_error("Failed to map buffer");
        return;
    }
    auto len = gst_buffer_get_size(buffer);
    if (len < sizeof(rtc::RtpHeader) || !track->isOpen()) {
        std::runtime_error("Invalid buffer size or track not open");
        return;
    }
    auto rtp = reinterpret_cast<rtc::RtpHeader *>(map.data);
    rtp->setSsrc(ssrc);
    track->send(reinterpret_cast<const std::byte *>(map.data), len);
    gst_buffer_unmap(buffer, &map);
}

std::string gathering_state_to_string(rtc::PeerConnection::GatheringState state) {
    switch (state) {
        case rtc::PeerConnection::GatheringState::New:
            return "New";
        case rtc::PeerConnection::GatheringState::InProgress:
            return "InProgress";
        case rtc::PeerConnection::GatheringState::Complete:
            return "Complete";
        default:
            std::runtime_error("Unknown gathering state");
            return "Unknown";
    }
}

void WebRtcResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/Offer_RTC", std::function<nlohmann::json()>([this]()
                                                              {
                                                                nlohmann::json ret = {
                                                                    {"rtc_status", gathering_state_to_string(this->gathering_state)},
                                                                    {"rtc_offer", this->m_config}};
                                                                return ret;//the m_config is the Ice offer
                                                              }));

    srv->Post("/Responce_RTC", std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                    {
                        rtc::Description answer(j_body["sdp"].get<std::string>(), j_body["type"].get<std::string>());
                        this->peer_connection->setRemoteDescription(answer);
                    }));
}