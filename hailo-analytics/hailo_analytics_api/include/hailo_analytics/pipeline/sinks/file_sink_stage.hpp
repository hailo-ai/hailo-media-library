#pragma once

// General includes
#include <algorithm>
#include <iostream>

// Media-Library includes
#include "media_library/encoder.hpp"

// Postporcess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/sinks/file_sink_module.hpp"

namespace hailo_analytics::pipeline::sinks
{

#define FILE_QUEUE_SIZE_DEFAULT (1)

class FileSinkStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::string m_filepath;
    EncodingType m_type;
    FileSinkModulePtr m_file;
    bool m_print_fps;

  public:
    FileSinkStage(std::string name, size_t queue_size = FILE_QUEUE_SIZE_DEFAULT, bool leaky = false,
                  bool trace_processing_operations = true, bool print_fps = false);

    AppStatus create(std::string filepath, EncodingType type, bool print_fps = false);

    AppStatus init() override;

    AppStatus deinit() override;

    AppStatus configure(std::string filepath, EncodingType type);

    AppStatus process(BufferPtr data) override;
};

class FileSinkStageBuild : public FileSinkStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = FILE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_print_fps = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_printfps_opt(bool activate);
        Builder &set_trace_opt(bool activate);
        std::shared_ptr<FileSinkStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
