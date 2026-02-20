#pragma once

#include <atomic>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <gst/gst.h>
#include <string>
#include <thread>
#include <mutex>
#include <queue>

#include <cstdint>
#include <cstddef>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"

using json = nlohmann::json;

class WebRTCStreamerExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension,
                          public hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver
{
    std::shared_ptr<rtc::PeerConnection> m_peer_connection;
    std::shared_ptr<rtc::Track> m_video_track;
    std::queue<GstSample *> m_sample_queue;
    bool m_is_streaming = false;
    std::thread m_streaming_thread;
    std::atomic<bool> m_connection_closed = false;
    mutable std::mutex m_connection_mutex;
    std::mutex m_queue_mutex;
    uint32_t m_ssrc;
    std::string m_session_id;

    // Simple connection tracking
    std::atomic<bool> m_client_connected = false;

  public:
    WebRTCStreamerExt() : m_ssrc(generate_unique_ssrc()), m_session_id(generate_session_id())
    {
        initialize_peer_connection();
    }

    ~WebRTCStreamerExt()
    {
        close_connection();
    }

    // Add your H264 sample to the streaming queue
    void on_rtp_packet(GstSample *sample, hailo_analytics::pipeline::sinks::rtp_session_id_t session_id) override
    {
        (void)session_id; // Unused parameter
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_is_streaming)
        {
            // Increment ref count since we're storing the sample and caller will unref
            gst_sample_ref(sample);
            m_sample_queue.push(sample);
        }
        // Note: Don't unref here - the caller owns the sample and will unref it
    }

    // Convenience overload for backward compatibility
    void send_rtp_packet(GstSample *sample)
    {
        on_rtp_packet(sample, m_session_id);
    }

    void close_connection()
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

        // Clear sample queue
        std::lock_guard<std::mutex> queueLock(m_queue_mutex);
        while (!m_sample_queue.empty())
        {
            gst_sample_unref(m_sample_queue.front());
            m_sample_queue.pop();
        }

        m_connection_closed = true;
        m_client_connected = false;
    }

    bool is_connection_closed() const
    {
        return m_connection_closed;
    }

    void reset_connection()
    {
        close_connection();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        initialize_peer_connection();

        HAILO_ANALYTICS_LOG_INFO("WebRTC connection reset");
    }

    // Check if we have an active client connection
    bool has_active_client() const
    {
        return m_client_connected && m_peer_connection &&
               m_peer_connection->state() == rtc::PeerConnection::State::Connected;
    }

    std::string create_offer()
    {

        // If we already have an active client, reset the connection first
        if (has_active_client())
        {
            HAILO_ANALYTICS_LOG_INFO("Existing client detected, resetting connection...");
            reset_connection();
        }
        else if (is_connection_closed())
        {
            reset_connection();
        }

        // Aaron TODO: Do we need to support H265 and H264 base on the stream configuration
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

    void handle_answer(const std::string &answerSdp)
    {
        rtc::Description answer(answerSdp, "answer");
        m_peer_connection->setRemoteDescription(answer);
        m_client_connected = true; // Mark client as connected when we receive answer
    }

    void handle_ice_candidate(const std::string &candidate, const std::string &sdpMid,
                              [[maybe_unused]] int sdpMLineIndex)
    {
        rtc::Candidate rtcCandidate(candidate, sdpMid);
        m_peer_connection->addRemoteCandidate(rtcCandidate);
    }

    // Get connection info for debugging if needed
    std::string get_connection_info() const
    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        json info = {{"hasActiveClient", has_active_client()},
                     {"isStreaming", m_is_streaming},
                     {"connectionClosed", m_connection_closed.load()},
                     {"clientConnected", m_client_connected.load()},
                     {"peerConnectionState", m_peer_connection ? static_cast<int>(m_peer_connection->state()) : -1}};
        return info.dump();
    }

    // Start the WebRTC streaming session
    hailo_analytics::pipeline::sinks::rtp_session_id_t start(std::string session_name)
    {
        (void)session_name; // Unused parameter
        return m_session_id;
    }

    void stop(hailo_analytics::pipeline::sinks::rtp_session_id_t session_id)
    {
        if (session_id == m_session_id)
        {
            close_connection();
        }
    }

    // Helper function to generate session id
    static std::string generate_session_id()
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

    std::string get_session_id() const
    {
        return m_session_id;
    }

  private:
    void streaming_loop()
    {
        while (m_is_streaming)
        {

            GstSample *sample = NULL;
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
                std::cerr << "WebRtc Streamer Failed to Map Buffer" << std::endl;
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

    void initialize_peer_connection()
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

    static uint32_t generate_unique_ssrc()
    {
        static std::atomic<uint32_t> ssrc_counter{1};
        return ssrc_counter.fetch_add(1);
    }
};
