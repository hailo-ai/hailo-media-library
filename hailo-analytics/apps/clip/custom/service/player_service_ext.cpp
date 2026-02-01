#include "player_service_ext.hpp"

std::shared_ptr<VideoStreamingServiceExt> VideoStreamingServiceExt::create(
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> webrtc_streamer)
{
    webrtc_streamer = webrtc_streamer ? webrtc_streamer : std::make_shared<WebRTCStreamerExt>();
    std::string session_id = webrtc_streamer->start("thumbnail");

    auto service = std::make_shared<VideoStreamingServiceExt>(session_id, webrtc_streamer);

    if (!service->initialize())
    {
        return nullptr;
    }

    return service;
}

VideoStreamingServiceExt::VideoStreamingServiceExt(
    const std::string &webrtc_session_id,
    std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> webrtc_streamer)
    : m_is_streaming(false), m_session_id(webrtc_session_id), m_webrtc_streamer(webrtc_streamer)
{
}

VideoStreamingServiceExt::~VideoStreamingServiceExt()
{
    stop_streaming();
    cleanup();
}

bool VideoStreamingServiceExt::initialize()
{
    std::lock_guard<std::mutex> lock(m_component_mutex);

    try
    {
        // Initialize MKVStreamer
        m_mkv_streamer = std::make_unique<MKVStreamer>();

        // Set up MKVStreamer callbacks
        m_mkv_streamer->set_rtp_packet_callback([this](const RtpPacketData &frame) { this->on_frame(frame); });

        m_mkv_streamer->set_end_of_stream_callback([this]() { this->on_end_of_stream(); });

        m_mkv_streamer->set_error_callback([this](const ErrorInfo &error) { this->on_error(error); });

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to initialize VideoStreamingServiceExt: " << e.what() << std::endl;
        return false;
    }
}

void VideoStreamingServiceExt::cleanup()
{
    std::lock_guard<std::mutex> lock(m_component_mutex);

    // Cleanup WebRTC
    if (m_webrtc_streamer)
    {
        m_webrtc_streamer->stop(m_session_id);
    }

    // Cleanup MKVStreamer
    m_mkv_streamer.reset();
}

bool VideoStreamingServiceExt::start_streaming(const std::vector<VideoFile> &video_files)
{
    if (video_files.empty())
    {
        std::cerr << "No video files provided" << std::endl;
        return false;
    }

    // Stop any existing streaming
    stop_streaming();

    std::lock_guard<std::mutex> lock(m_status_mutex);

    // Start MKV streaming
    if (!m_mkv_streamer->start_streaming(video_files))
    {
        std::cerr << "Failed to start MKV streaming" << std::endl;
        return false;
    }

    // Can start streaming
    m_is_streaming = true;

    std::cout << "Video streaming started with " << video_files.size() << " files" << std::endl;
    return true;
}

void VideoStreamingServiceExt::stop_streaming()
{
    std::lock_guard<std::mutex> lock(m_status_mutex);

    if (!m_is_streaming)
    {
        return;
    }

    std::cout << "Stopping video streaming..." << std::endl;

    // Stop MKV streaming
    if (m_mkv_streamer)
    {
        m_mkv_streamer->stop_streaming();
    }

    m_is_streaming = false;

    std::cout << "Video streaming stopped" << std::endl;
}

bool VideoStreamingServiceExt::is_streaming() const
{
    return m_is_streaming;
}

std::shared_ptr<hailo_analytics::pipeline::sinks::RTPConverterStage::RTPReceiver> VideoStreamingServiceExt::
    get_webrtc_streamer() const
{
    return m_webrtc_streamer;
}

void VideoStreamingServiceExt::on_frame(const RtpPacketData &frame)
{

    if (!m_is_streaming)
    {
        return;
    }

    if (frame.sample != nullptr)
    {
        m_webrtc_streamer->on_rtp_packet(frame.sample, m_session_id);
    }
}

void VideoStreamingServiceExt::on_end_of_stream()
{
    std::cout << "End of stream reached" << std::endl;

    // Auto-stop streaming when all files are processed
    stop_streaming();
}

void VideoStreamingServiceExt::on_error(const ErrorInfo &error)
{
    std::cerr << "MKVStreamer error: " << error.message << std::endl;

    // Stop streaming on error
    stop_streaming();
}
