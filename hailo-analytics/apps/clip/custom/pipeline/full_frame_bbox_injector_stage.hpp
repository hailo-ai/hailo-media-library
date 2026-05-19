#pragma once

#include <stddef.h>
#include <chrono>
#include <memory>
#include <string>
#include <optional>

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

using hailo_analytics::pipeline::Buffer;
using hailo_analytics::pipeline::BufferPtr;

#define FULL_FRAME_BBOX_INJECTOR_QUEUE_SIZE_DEFAULT 5

class FullFrameBBoxInjectorStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    FullFrameBBoxInjectorStage(std::string name, std::string class_name, float interval_seconds = 5.0f,
                               size_t queue_size = FULL_FRAME_BBOX_INJECTOR_QUEUE_SIZE_DEFAULT, bool leaky = false,
                               bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;
    hailo_analytics::pipeline::AppStatus deinit() override;
    hailo_analytics::pipeline::AppStatus process(BufferPtr data);

  private:
    std::string m_class_name;
    float m_interval_seconds;
    bool m_first_frame;
    std::chrono::steady_clock::time_point m_last_inject_time;
};

class FullFrameBBoxInjectorStageBuild : public FullFrameBBoxInjectorStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_class_name;
        float m_interval_seconds = 5.0f;
        size_t m_queue_size = FULL_FRAME_BBOX_INJECTOR_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_full_frame_class_name(std::string class_name);
        Builder &set_interval_seconds(float seconds);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<FullFrameBBoxInjectorStage> buildptr() const;
    };

    static Builder create();
};
