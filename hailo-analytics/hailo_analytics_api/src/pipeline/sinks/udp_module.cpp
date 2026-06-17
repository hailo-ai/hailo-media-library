// General includes
#include <gst/gst.h>
#include <tl/expected.hpp>
#include <gst/gstparse.h>
#include <stddef.h>
#include <iostream>
#include <memory>
#include <string>

// Infra includes
#include "hailo_analytics/pipeline/sinks/udp_module.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"

namespace hailo_analytics::pipeline::sinks
{
constexpr int UDP_SRC_QUEUE_MAX_BUFFERS = 5;

tl::expected<UdpModulePtr, AppStatus> UdpModule::create(std::string name, std::string host, std::string port,
                                                        EncodingType type, bool print_fps)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    UdpModulePtr udp_module = std::make_shared<UdpModule>(name, host, port, type, status, print_fps);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return udp_module;
}

UdpModule::UdpModule(std::string name, std::string host, std::string port, EncodingType type, AppStatus &status,
                     bool print_fps)
    : OutputModule(name, type, print_fps), m_host(host), m_port(port)
{
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed create UDP pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    this->OutputModule::set_gst_callbacks(UDP_MODULE_SRC_NAME);

    status = AppStatus::SUCCESS;
}

UdpModule::~UdpModule()
{
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 */
std::string UdpModule::create_pipeline_string()
{
    std::string pipeline = "";

    std::string caps_type;
    std::string rtp_payloader;
    std::string encoding_name;
    if (m_type == EncodingType::H264)
    {
        caps_type = "video/x-h264";
        rtp_payloader = "rtph264pay";
        encoding_name = "H264";
    }
    else
    {
        caps_type = "video/x-h265";
        rtp_payloader = "rtph265pay";
        encoding_name = "H265";
    }

    std::ostringstream caps2;
    caps2 << caps_type << ",framerate=30/1,stream-format=byte-stream,alignment=au";

    std::ostringstream udp_sink;
    udp_sink << "udpsink host=" << m_host << " port=" << m_port;

    pipeline = "appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 "
               "max-buffers=1 name=" +
               std::string(UDP_MODULE_SRC_NAME) +
               " ! "
               "queue name=" +
               std::string(UDP_MODULE_SRC_QUEUE_NAME) +
               " leaky=downstream max-size-buffers=" + std::to_string(UDP_SRC_QUEUE_MAX_BUFFERS) +
               " max-size-bytes=0 max-size-time=0 ! " + caps2.str() + " ! " +
               "tee name=udp_tee "
               "udp_tee. ! "
               "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! " +
               rtp_payloader + " ! application/x-rtp, media=video, encoding-name=" + encoding_name + " ! " +
               udp_sink.str() +
               " name=udp_sink sync=true "
               "udp_tee. ! "
               "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
               "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=fpsdisplaysink "
               "text-overlay=false sync=true video-sink=fakesink ";

    HAILO_ANALYTICS_LOG_INFO("Pipeline: {}", pipeline);

    return pipeline;
}

} // namespace hailo_analytics::pipeline::sinks
