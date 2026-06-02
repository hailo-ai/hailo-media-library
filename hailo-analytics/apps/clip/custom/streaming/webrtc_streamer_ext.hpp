#pragma once

#include <nlohmann/json.hpp>
#include <gst/gst.h>
#include <rtc/rtc.hpp>
#include <atomic>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <cstdint>
#include <memory>

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
    std::string m_stream_name; // Store the stream name for stop() comparison

  public:
    WebRTCStreamerExt();

    ~WebRTCStreamerExt();

    // Add your H264 sample to the streaming queue
    void on_rtp_packet(GstSample *sample, hailo_analytics::pipeline::sinks::rtp_session_id_t session_id) override;

    // Convenience overload for backward compatibility
    void send_rtp_packet(GstSample *sample);

    void close_connection();

    bool is_connection_closed() const;

    void reset_connection();

    // Check if we have an active client connection
    bool has_active_client() const;

    std::string create_offer();

    void handle_answer(const std::string &answerSdp);

    void handle_ice_candidate(const std::string &candidate, const std::string &sdpMid, int /*sdpMLineIndex*/);

    // Get connection info for debugging if needed
    std::string get_connection_info() const;

    // Start the WebRTC streaming session
    hailo_analytics::pipeline::sinks::rtp_session_id_t start(std::string session_name);

    void stop(hailo_analytics::pipeline::sinks::rtp_session_id_t session_id);

    // Helper function to generate session id
    static std::string generate_session_id();

    std::string get_session_id() const;

  private:
    void streaming_loop();

    void initialize_peer_connection();

    static uint32_t generate_unique_ssrc();
};
