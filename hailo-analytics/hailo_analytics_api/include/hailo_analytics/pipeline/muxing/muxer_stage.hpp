#pragma once

/**
 * @file muxer_stage.hpp
 * @brief Stage that multiplexes multiple video streams into a single output stream.
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

/**
 * @class MuxerStage
 * @brief Class responsible for handling multiplexing of multiple video streams into a single output stream.
 */
class MuxerStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::string m_main_inlet_name;
    size_t m_main_queue_size;
    std::string m_sub_inlet_name;
    size_t m_sub_queue_size;

  public:
    MuxerStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
               std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky,
               bool trace_processing_operations = true);

    void add_queue(std::string name) override;
    void loop() override;
};

/**
 * @brief Builder-based muxer stage for simplified construction.
 *
 * Provides a builder pattern interface for creating muxer stages with
 * configurable parameters for multiplexing streams.
 */
class MuxerStageBuild : public MuxerStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_main_inlet_name;
        size_t m_main_queue_size = 10;
        bool m_main_queue_leaky = false;
        std::optional<std::string> m_sub_inlet_name;
        size_t m_sub_queue_size = 10;
        bool m_sub_queue_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_main_inlet_name(std::string name);
        Builder &set_main_queue_size(size_t size);
        Builder &set_main_leaky(bool leaky);
        Builder &set_sub_inlet_name(std::string name);
        Builder &set_sub_queue_size(size_t size);
        Builder &set_sub_leaky(bool leaky);
        Builder &set_trace_opt(bool activate);
        std::shared_ptr<MuxerStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::muxing
