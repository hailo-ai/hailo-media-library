// NOTE: This file is a verbatim copy of
// hailo-analytics/apps/clip/custom/streaming/webrtc_streamer_ext.cpp.
// We will need to move webrtc_streamer_ext.cpp so that both clip and vlm can share

#include "webrtc_streamer_ext.hpp"

#include <random>

WebRTCStreamerExt::WebRTCStreamerExt() : m_ssrc(generate_unique_ssrc()), m_session_id(generate_session_id())
{
    initialize_peer_connection();
}

WebRTCStreamerExt::~WebRTCStreamerExt()
{
    close_connection();
}

void WebRTCStreamerExt::on_rtp_packet(GstSample *sample, hailo_analytics::pipeline::sinks::rtp_session_id_t session_id)
{
    (void)session_id;
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    if (m_is_streaming)
    {
        gst_sample_ref(sample);
        m_sample_queue.push(sample);
    }
}

void WebRTCStreamerExt::send_rtp_packet(GstSample *sample)
{
    on_rtp_packet(sample, m_session_id);
}

void WebRTCStreamerExt::close_connection()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    if (m_is_streaming)
    {
        m_is_streaming = false;
        if (m_streaming_thread.joinable())
            m_streaming_thread.join();
    }

    if (m_video_track)
    {
        m_video_track->close();
        m_video_track.reset();
    }

    if (m_peer_connection && m_peer_connection->state() == rtc::PeerConnection::State::Connected)
        m_peer_connection->close();

    std::lock_guard<std::mutex> queueLock(m_queue_mutex);
    while (!m_sample_queue.empty())
    {
        gst_sample_unref(m_sample_queue.front());
        m_sample_queue.pop();
    }

    m_connection_closed = true;
    m_client_connected = false;
}

bool WebRTCStreamerExt::is_connection_closed() const
{
    return m_connection_closed;
}

void WebRTCStreamerExt::reset_connection()
{
    close_connection();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    initialize_peer_connection();

    HAILO_ANALYTICS_LOG_INFO("WebRTC connection reset");
}

bool WebRTCStreamerExt::has_active_client() const
{
    return m_client_connected && m_peer_connection &&
           m_peer_connection->state() == rtc::PeerConnection::State::Connected;
}

std::string WebRTCStreamerExt::create_offer()
{
    if (has_active_client())
    {
        HAILO_ANALYTICS_LOG_INFO("Existing client detected, resetting connection...");
        reset_connection();
    }
    else if (is_connection_closed())
    {
        reset_connection();
    }

    rtc::Description::Video videoDesc("video", rtc::Description::Direction::SendOnly);
    videoDesc.addH264Codec(96);
    videoDesc.addSSRC(m_ssrc, "video-send");
    m_video_track = m_peer_connection->addTrack(videoDesc);
    m_peer_connection->setLocalDescription();

    if (!m_is_streaming)
    {
        m_is_streaming = true;
        m_streaming_thread = std::thread(&WebRTCStreamerExt::streaming_loop, this);
    }

    auto localDesc = m_peer_connection->localDescription();
    json offerJson = {{"type", "offer"}, {"sdp", localDesc->generateSdp()}};
    return offerJson.dump();
}

void WebRTCStreamerExt::handle_answer(const std::string &answerSdp)
{
    rtc::Description answer(answerSdp, "answer");
    m_peer_connection->setRemoteDescription(answer);
    m_client_connected = true;
}

void WebRTCStreamerExt::handle_ice_candidate(const std::string &candidate, const std::string &sdpMid,
                                             int /*sdpMLineIndex*/)
{
    rtc::Candidate rtcCandidate(candidate, sdpMid);
    m_peer_connection->addRemoteCandidate(rtcCandidate);
}

std::string WebRTCStreamerExt::get_connection_info() const
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    json info = {{"hasActiveClient", has_active_client()},
                 {"isStreaming", m_is_streaming},
                 {"connectionClosed", m_connection_closed.load()},
                 {"clientConnected", m_client_connected.load()},
                 {"peerConnectionState", m_peer_connection ? static_cast<int>(m_peer_connection->state()) : -1}};
    return info.dump();
}

hailo_analytics::pipeline::sinks::rtp_session_id_t WebRTCStreamerExt::start(std::string session_name)
{
    m_stream_name = session_name;
    return m_session_id;
}

void WebRTCStreamerExt::stop(hailo_analytics::pipeline::sinks::rtp_session_id_t session_id)
{
    if (session_id == m_session_id)
    {
        close_connection();
    }
}

std::string WebRTCStreamerExt::generate_session_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::string session_id;
    for (int i = 0; i < 16; ++i)
    {
        session_id += "0123456789abcdef"[dis(gen)];
    }
    return session_id;
}

std::string WebRTCStreamerExt::get_session_id() const
{
    return m_session_id;
}

void WebRTCStreamerExt::streaming_loop()
{
    while (m_is_streaming)
    {
        GstSample *sample = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (!m_sample_queue.empty())
            {
                sample = m_sample_queue.front();
                m_sample_queue.pop();
            }
        }

        if (!sample)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo mapInfo;
        if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ))
        {
            gst_sample_unref(sample);
            HAILO_ANALYTICS_LOG_ERROR("WebRtc Streamer Failed to Map Buffer");
            continue;
        }

        if (m_peer_connection->state() != rtc::PeerConnection::State::Connected || !m_video_track ||
            !m_video_track->isOpen())
        {
            gst_buffer_unmap(buffer, &mapInfo);
            gst_sample_unref(sample);
            continue;
        }

        if (mapInfo.size >= sizeof(rtc::RtpHeader))
        {
            auto *rtpHeader = reinterpret_cast<rtc::RtpHeader *>(mapInfo.data);
            rtpHeader->setSsrc(m_ssrc);

            m_video_track->send(reinterpret_cast<const std::byte *>(mapInfo.data), mapInfo.size);
        }
        else
        {
            HAILO_ANALYTICS_LOG_INFO("packet less than rtp header size!!");
        }
        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);
    }
}

void WebRTCStreamerExt::initialize_peer_connection()
{
    rtc::Configuration config;
    config.bindAddress = "10.0.0.1";
    config.iceServers.clear();

    m_peer_connection = std::make_shared<rtc::PeerConnection>(config);

    m_peer_connection->onStateChange([this](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed)
        {
            m_connection_closed = true;
            m_client_connected = false;
        }
    });

    m_peer_connection->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        HAILO_ANALYTICS_LOG_INFO("Gathering State: {}", static_cast<int>(state));
    });

    m_connection_closed = false;
}

uint32_t WebRTCStreamerExt::generate_unique_ssrc()
{
    static std::atomic<uint32_t> ssrc_counter{1};
    return ssrc_counter.fetch_add(1);
}
