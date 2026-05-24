#include "hailo_analytics/pipeline/sinks/file_sink_module.hpp"

#include <gst/gst.h>
#include <gst/gstparse.h>
#include <stddef.h>
#include <iostream>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace hailo_analytics::pipeline::sinks
{

tl::expected<FileSinkModulePtr, AppStatus> FileSinkModule::create(std::string name, std::string filepath,
                                                                  EncodingType type, bool print_fps)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    FileSinkModulePtr file_module = std::make_shared<FileSinkModule>(name, filepath, type, status, print_fps);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return file_module;
}

FileSinkModule::FileSinkModule(std::string name, std::string filepath, EncodingType type, AppStatus &status,
                               bool print_fps)
    : OutputModule(name, type, print_fps), m_filepath(filepath), m_type(type)
{
    // Initialize gstreamer
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed create file pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    this->OutputModule::set_gst_callbacks(FILE_SOURCE);

    status = AppStatus::SUCCESS;
}

std::string FileSinkModule::create_pipeline_string()
{
    std::string pipeline = "";

    std::string caps_type;
    std::string encoder_parser;
    if (m_type == EncodingType::H264)
    {
        caps_type = "video/x-h264";
        encoder_parser = "h264parse";
    }
    else
    {
        caps_type = "video/x-h265";
        encoder_parser = "h265parse";
    }

    std::ostringstream caps2;
    caps2 << caps_type << ",framerate=30/1,stream-format=byte-stream,alignment=au";

    std::ostringstream file_sink;
    file_sink << "filesink location=" << m_filepath << " name=file_sink ";

    pipeline = "appsrc name=file_src do-timestamp=true format=time block=true is-live=true max-bytes=0 max-buffers=1 ! "
               "queue name=" +
               std::string(SRC_QUEUE_NAME) + " leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
               caps2.str() +
               " ! "
               "tee name=file_tee "
               "file_tee. ! "
               "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
               "fpsdisplaysink name=fpsdisplaysink sync=false video-sink=fakesink "
               "file_tee. ! "
               "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! " +
               encoder_parser + " ! " + file_sink.str() + " sync=false ";

    HAILO_ANALYTICS_LOG_INFO("Pipeline: {}", pipeline);

    return pipeline;
}

} // namespace hailo_analytics::pipeline::sinks
