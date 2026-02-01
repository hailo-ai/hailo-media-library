#pragma once

/**
 * @file demuxer_stage.hpp
 * @brief Stage that demultiplexes a single video stream into multiple output streams.
 **/

// General includes
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>
#include <cstddef>
#include <atomic>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::muxing
{

class DemuxerStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::string m_main_outlet_name;
    std::string m_sub_outlet_name;
    bool m_copy_roi_metadata;

  public:
    DemuxerStage(std::string name, std::string main_outlet_name, std::string sub_outlet_name, size_t queue_size = 10,
                 bool leaky = false, bool trace_processing_operations = true, bool copy_roi_metadata = false);

    AppStatus process(BufferPtr buffer) override;

  private:
    void copy_hailo_roi_metadata(BufferPtr main_buffer, BufferPtr sub_buffer);
};

class DemuxerStageBuild : public DemuxerStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_main_outlet_name;
        std::optional<std::string> m_sub_outlet_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_trace = true;
        bool m_copy_roi_metadata = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_main_outlet_name(std::string name);
        Builder &set_sub_outlet_name(std::string name);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool leaky);
        Builder &set_trace_opt(bool activate);
        Builder &set_copy_roi_metadata_opt(bool copy_roi);
        std::shared_ptr<DemuxerStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::muxing
