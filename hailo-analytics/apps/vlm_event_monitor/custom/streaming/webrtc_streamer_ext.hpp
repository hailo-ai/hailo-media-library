#pragma once

// NOTE: This file is a verbatim copy of
// hailo-analytics/apps/clip/custom/streaming/webrtc_streamer_ext.hpp.
// It is duplicated here intentionally to keep the VLM app standalone in pahse 1
// and avoid any modifications to the clip tree. A formal consolidation into
// hailo_analytics_api/streaming/ is tracked as near future phase stage implementation

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
    std::string m_stream_name;

  public:
    WebRTCStreamerExt();

    ~WebRTCStreamerExt();

    void on_rtp_packet(GstSample *sample, hailo_analytics::pipeline::sinks::rtp_session_id_t session_id) override;

    void send_rtp_packet(GstSample *sample);

    void close_connection();

    bool is_connection_closed() const;

    void reset_connection();

    bool has_active_client() const;

    std::string create_offer();

    void handle_answer(const std::string &answerSdp);

    void handle_ice_candidate(const std::string &candidate, const std::string &sdpMid, int /*sdpMLineIndex*/);

    std::string get_connection_info() const;

    hailo_analytics::pipeline::sinks::rtp_session_id_t start(std::string session_name) override;

    void stop(hailo_analytics::pipeline::sinks::rtp_session_id_t session_id) override;

    static std::string generate_session_id();

    std::string get_session_id() const;

  private:
    void streaming_loop();

    void initialize_peer_connection();

    static uint32_t generate_unique_ssrc();
};
