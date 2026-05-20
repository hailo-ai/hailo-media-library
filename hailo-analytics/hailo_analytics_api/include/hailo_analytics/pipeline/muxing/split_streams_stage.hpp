#pragma once

/**
 * @file split_streams_stage.hpp
 * @brief Stage that splits a bundled buffer back into per-stream paths by stream id.
 **/

#include <stddef.h>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

namespace hailo_analytics::pipeline::muxing
{

/**
 * @brief Splits a bundled buffer back into per-stream paths.
 *
 * Reads the carrier's ATTACHED_STREAM metadatas, strips them, and dispatches the carrier and
 * each passenger to its subscriber. Routing uses the 3-arg
 * `PipelineBuilder::connect(split, stream_id, subscriber)` so each subscriber carries its
 * stream id, and SplitStreamsStage dispatches by stream id at runtime.
 *
 * Optionally propagates the AI postprocess tree: when enabled, the carrier's HailoROI is
 * shared onto every passenger's analytics Buffer before dispatch (shallow share).
 */
class SplitStreamsStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    SplitStreamsStage(std::string name, std::string carrier_stream_id, bool propagate_roi, size_t queue_size,
                      bool leaky, bool trace_processing_operations = true);

    AppStatus init() override;
    AppStatus process(hailo_analytics::pipeline::BufferPtr buffer) override;

  private:
    std::string m_carrier_stream_id;
    bool m_propagate_roi;
    // Populated by init() from the registered subscribers' stream_ids (everything except the
    // carrier). process() validates every incoming bundle against this derived roster.
    std::set<std::string> m_passenger_stream_ids;
};

class SplitStreamsStageBuild : public SplitStreamsStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_carrier_stream_id;
        bool m_propagate_roi = false;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_carrier_stream_id(std::string id);
        Builder &set_propagate_roi_opt(bool activate);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<SplitStreamsStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::muxing
