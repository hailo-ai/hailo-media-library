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

/**
 * @brief Default queue size for tracker stage
 */
inline constexpr size_t TRACKER_QUEUE_SIZE_DEFAULT = 5;

/**
 * @brief Alias for tracker parameters
 */
using TrackerParams = HailoLightweightTracker::TrackerParams;

/**
 * @brief Stage for tracking detections across frames using a lightweight tracker
 *
 * This stage applies object tracking to detections in the pipeline, maintaining
 * consistent tracking IDs for detected objects across frames. It supports per-class
 * tracker configurations and can filter out non-tracked classifications.
 *
 * Features:
 * - Per-class tracker instances with independent parameters
 * - Configurable tracking parameters (IOU threshold, history size, etc.)
 * - Optional blocking of non-tracked object classes
 * - Lightweight implementation suitable for real-time processing
 */
class LightweightTrackerStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::vector<int> m_classification_ids;
    bool m_block_non_tracked_class_id;
    std::map<int, std::unique_ptr<HailoLightweightTracker>> m_trackers;

  public:
    /**
     * @brief Constructor for LightweightTrackerStage
     * @param name Stage name for identification
     * @param tracker_params Map of class IDs to tracker parameters
     * @param queue_size Size of the processing queue (default: TRACKER_QUEUE_SIZE_DEFAULT)
     * @param leaky Whether the queue should drop old frames when full (default: false)
     * @param classification_ids Vector of class IDs to track (default: empty)
     * @param block_non_tracked_class If true, removes detections not in classification_ids (default: false)
     * @param trace_processing_operations Enable tracing for processing operations (default: true)
     */
    LightweightTrackerStage(std::string name, std::map<int, TrackerParams> tracker_params,
                            size_t queue_size = TRACKER_QUEUE_SIZE_DEFAULT, bool leaky = false,
                            std::vector<int> classification_ids = {}, bool block_non_tracked_class = false,
                            bool trace_processing_operations = true);

    /**
     * @brief Process buffer and apply tracking to detections
     * @param data Buffer containing detection data to track
     * @return AppStatus indicating success or failure
     */
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder pattern implementation for LightweightTrackerStage
 *
 * Provides a fluent interface for constructing LightweightTrackerStage instances
 * with configurable tracker parameters. Supports per-class configuration or
 * global configuration (when class_id = -1) that applies to all tracked classes.
 */
class LightweightTrackerStageBuild : public LightweightTrackerStage
{
  public:
    /**
     * @brief Builder class for constructing LightweightTrackerStage instances
     */
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
        /**
         * @brief Set the stage name
         * @param name Name for the stage
         * @return Builder reference for chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the queue size
         * @param size Size of the processing queue
         * @return Builder reference for chaining
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Set the leaky option
         * @param activate If true, queue drops old frames when full
         * @return Builder reference for chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set the classification IDs to track
         * @param ids Vector of class IDs to track
         * @return Builder reference for chaining
         */
        Builder &set_classification_ids(const std::vector<int> &ids);

        /**
         * @brief Set the trace option
         * @param activate If true, enables tracing for processing operations
         * @return Builder reference for chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Set the history size for tracker
         * @param history_size Number of frames to keep in tracking history
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_history_size(size_t history_size, int class_id = -1);

        /**
         * @brief Set the smoothing alpha parameter
         * @param smooth_alpha Alpha value for exponential smoothing (0.0 to 1.0)
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_smooth_alpha(float smooth_alpha, int class_id = -1);

        /**
         * @brief Set the IOU threshold for matching detections to tracks
         * @param iou_threshold Minimum IOU value for matching (0.0 to 1.0)
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_iou_threshold(float iou_threshold, int class_id = -1);

        /**
         * @brief Set the weighted average decay parameter
         * @param weighted_average_decay Decay factor for weighted averaging
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_weighted_average_decay(float weighted_average_decay, int class_id = -1);

        /**
         * @brief Set the grid size for spatial hashing
         * @param grid_size Size of grid for spatial hashing optimization
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_grid_size(size_t grid_size, int class_id = -1);

        /**
         * @brief Set the grace period for tracks
         * @param grace_period Number of frames to keep tracks alive without detection
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_grace_period(int grace_period, int class_id = -1);

        /**
         * @brief Set whether to add tracking IDs to detections
         * @param add_tracking_id If true, adds tracking ID to detection objects
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_add_tracking_id(bool add_tracking_id, int class_id = -1);

        /**
         * @brief Set whether to copy nested objects during tracking
         * @param copy_nested_objects If true, copies nested objects from detections
         * @param class_id Class ID to configure, or -1 for all classes (default: -1)
         * @return Builder reference for chaining
         */
        Builder &set_copy_nested_objects(bool copy_nested_objects, int class_id = -1);

        /**
         * @brief Set whether to block non-tracked classification IDs
         * @param block If true, removes detections not in classification_ids list
         * @return Builder reference for chaining
         */
        Builder &set_block_non_tracked_classification_id(bool block);

        /**
         * @brief Build and return shared pointer to LightweightTrackerStage
         * @return Shared pointer to constructed LightweightTrackerStage
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<LightweightTrackerStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder instance
     * @return Builder instance for constructing LightweightTrackerStage
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
