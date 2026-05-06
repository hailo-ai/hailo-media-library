#pragma once

#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

#include "streaming/mkv_streamer.hpp"
#include "streaming/webrtc_streamer_ext.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"

class VideoStreamingServiceExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension
{
  public:
    // Factory method to create an instance
    static std::shared_ptr<VideoStreamingServiceExt> create(
        std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> webrtc_streamer = nullptr);

    VideoStreamingServiceExt(
        const std::string &webrtc_session_id,
        std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> webrtc_streamer);
    ~VideoStreamingServiceExt();

    // Main streaming control
    bool start_streaming(const std::vector<VideoFile> &video_files);
    void stop_streaming();
    bool is_streaming() const;

    // WebRTC access for web service
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> get_webrtc_streamer() const;

    const std::string &get_session_id() const;

  private:
    // Initialization methods
    bool initialize();
    void cleanup();

    // Components
    std::unique_ptr<MKVStreamer> m_mkv_streamer;

    // Current streaming state
    std::atomic<bool> m_is_streaming;
    mutable std::mutex m_status_data_mutex;

    // Session management
    std::string m_session_id;
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> m_webrtc_streamer;

    // Threading synchronization
    mutable std::mutex m_status_mutex;
    mutable std::mutex m_component_mutex;

    // MKVStreamer callbacks
    void on_frame(const RtpPacketData &frame);
    void on_end_of_stream();
    void on_error(const ErrorInfo &error);
};
