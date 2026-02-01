#pragma once

// General includes
#include <map>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/tracking/hailo_lightweight_tracker.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

inline constexpr size_t TRACKER_QUEUE_SIZE_DEFAULT = 5;

using TrackerParams = HailoLightweightTracker::TrackerParams;

class LightweightTrackerStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::vector<int> m_classification_ids;
    bool m_block_non_tracked_class_id;
    std::map<int, std::unique_ptr<HailoLightweightTracker>> m_trackers;

  public:
    LightweightTrackerStage(std::string name, std::map<int, TrackerParams> tracker_params,
                            size_t queue_size = TRACKER_QUEUE_SIZE_DEFAULT, bool leaky = false,
                            std::vector<int> classification_ids = {}, bool block_non_tracked_class = false,
                            bool trace_processing_operations = true);

    AppStatus process(BufferPtr data) override;
};

class LightweightTrackerStageBuild : public LightweightTrackerStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = TRACKER_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        std::vector<int> m_classification_ids = {};
        bool m_block_non_tracked_class_id = false;
        bool m_trace = true;
        std::map<int, TrackerParams> m_tracker_params = {};

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_classification_ids(const std::vector<int> &ids);
        Builder &set_trace_opt(bool activate);

        Builder &set_history_size(size_t history_size, int class_id = -1);
        Builder &set_smooth_alpha(float smooth_alpha, int class_id = -1);
        Builder &set_iou_threshold(float iou_threshold, int class_id = -1);
        Builder &set_weighted_average_decay(float weighted_average_decay, int class_id = -1);
        Builder &set_grid_size(size_t grid_size, int class_id = -1);
        Builder &set_grace_period(int grace_period, int class_id = -1);
        Builder &set_add_tracking_id(bool add_tracking_id, int class_id = -1);
        Builder &set_copy_nested_objects(bool copy_nested_objects, int class_id = -1);
        Builder &set_block_non_tracked_classification_id(bool block);

        std::shared_ptr<LightweightTrackerStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
