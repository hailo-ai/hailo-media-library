#pragma once

#include <tl/expected.hpp>
#include <memory>
#include <string>

#include "hailo_analytics/pipeline/sinks/output_module.hpp"
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
