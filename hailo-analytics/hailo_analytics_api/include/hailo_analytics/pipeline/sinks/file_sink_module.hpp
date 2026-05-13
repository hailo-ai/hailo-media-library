#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// Postporcess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

#define FILE_SOURCE "file_src"

namespace hailo_analytics::pipeline::sinks
{

class FileSinkModule;
using FileSinkModulePtr = std::shared_ptr<FileSinkModule>;

class FileSinkModule : public OutputModule
{
  private:
    std::string m_filepath;
    EncodingType m_type;

  public:
    static tl::expected<FileSinkModulePtr, AppStatus> create(std::string name, std::string filepath, EncodingType type,
                                                             bool print_fps);
    ~FileSinkModule() override = default;
    FileSinkModule(std::string name, std::string filepath, EncodingType type, AppStatus &status, bool print_fps);

  private:
    std::string create_pipeline_string();
};

} // namespace hailo_analytics::pipeline::sinks
