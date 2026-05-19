#pragma once

#include <stddef.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
// Postporcess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

namespace hailo_analytics::pipeline::ai
{

class PersistStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::vector<HailoDetectionPtr> m_detections;
    size_t m_expiration_threshold;
    size_t m_count = 0;
    bool m_print_fps;

  public:
    PersistStage(std::string name, size_t expiration = 5, size_t queue_size = 5, bool leaky = false,
                 bool trace_processing_operations = true, bool print_fps = false);

    AppStatus process(BufferPtr data) override;
};

class PersistStageBuild : public PersistStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_expiration = 5;
        size_t m_queue_size = 5;
        bool m_leaky = false;
        bool m_print_fps = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_expiration_opt(size_t expiration);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_printfps_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<PersistStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
