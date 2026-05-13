#pragma once
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo/hailo_postprocess_tools/tracking/hailo_tracker.hpp"

namespace hailo_analytics::pipeline::ai
{

static constexpr size_t TRACKER_STAGE_QUEUE_SIZE_DEFAULT = 5;

class TrackerStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::string m_tracker_name = "hailo_tracker";
    HailoTrackerParams m_tracker_params;
    int m_class_id;
    bool m_block_non_tracked_class_id;
    bool m_print_fps;

  public:
    TrackerStage(std::string name, size_t queue_size = TRACKER_STAGE_QUEUE_SIZE_DEFAULT, bool leaky = false,
                 int classification_id = -1, bool block_non_tracked_class_id = false,
                 bool trace_processing_operations = true, bool print_fps = false);

    AppStatus init() override;

    AppStatus deinit() override;

    AppStatus process(BufferPtr data) override;
};

class TrackerStageBuild : public TrackerStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = TRACKER_STAGE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        int m_classification_id = 1;
        bool m_block_non_tracked_class_id = false;
        bool m_print_fps = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_classification_id(int id);
        Builder &set_block_non_tracked_classification_id(bool block);
        Builder &set_printfps_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<TrackerStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
