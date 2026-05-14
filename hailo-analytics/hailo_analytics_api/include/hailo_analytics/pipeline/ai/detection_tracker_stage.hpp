#pragma once

// General includes
#include <fstream>
#include <map>
#include <string>
#include <vector>

// HailoRT tracker C API
#include "hailopp/hailotracker.h"

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::ai
{

inline constexpr size_t DETECTION_TRACKER_QUEUE_SIZE_DEFAULT = 5;

/**
 * @brief Stage for tracking detections across frames using the HailoRT tracker.
 *
 * This stage applies multi-object tracking to detections in the pipeline using
 * the HailoRT C tracker library, maintaining consistent tracking IDs for
 * detected objects across frames. It produces tracked detections on every
 * output buffer, including frames that were not processed by the AI stage.
 *
 * Features:
 * - Kalman filter based state estimation and prediction
 * - Configurable association parameters (IoU weight, thresholds, etc.)
 * - EMA bounding box smoothing
 *
 * @note This stage replaces all input HailoDetection objects with newly created
 *       ones from the tracker's predicted tracklets. Any sub-objects attached to
 *       the original detections will be lost. This stage is intended for use as
 *       part of the first-stage detection pipeline (directly after AI + aggregator)
 *       where detections carry no user-added data. Using it at a later pipeline
 *       stage may result in data loss.
 */
class DetectionTrackerStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    hailo_tracker m_tracker_handle;
    hailo_tracker_config_t m_tracker_config;
    std::map<uint8_t, std::string> m_labels_map;

    void write_mot_line(const std::vector<HailoDetectionPtr> &detections);

  protected:
    // MOT Challenge output
    bool m_save_mot_output;
    std::string m_mot_output_path;
    std::ofstream m_mot_file;
    uint64_t m_frame_counter;

    static std::vector<uint8_t> hailo_detections_to_hailort_detections(
        const std::vector<HailoDetectionPtr> &detections);
    std::vector<HailoDetectionPtr> tracklets_to_hailo_detections(const hailo_tracklets_t &tracklets) const;

    AppStatus update_tracker_from_roi_detections(HailoROIPtr hailo_roi);
    AppStatus predict_tracklets(hailo_tracklets_t &tracklets);

  public:
    /**
     * @brief Construct a DetectionTrackerStage.
     * @param name Stage name for identification
     * @param config HailoRT tracker configuration
     * @param labels_map Mapping from class_id to human-readable label
     * @param queue_size Size of the processing queue
     * @param leaky Whether the queue drops old frames when full
     * @param trace_processing_operations Enable performance tracing
     * @param mot_output_path Path to write MOT Challenge CSV output (empty = disabled). Should be used for debug only.
     */
    DetectionTrackerStage(std::string name, hailo_tracker_config_t config, std::map<uint8_t, std::string> labels_map,
                          size_t queue_size = DETECTION_TRACKER_QUEUE_SIZE_DEFAULT, bool leaky = false,
                          bool trace_processing_operations = true, std::string mot_output_path = "");

    ~DetectionTrackerStage() override;

    AppStatus init() override;
    AppStatus deinit() override;
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder pattern for constructing DetectionTrackerStage instances.
 */
class DetectionTrackerStageBuild : public DetectionTrackerStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = DETECTION_TRACKER_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;
        hailo_tracker_config_t m_tracker_config = HAILO_TRACKER_CONFIG_DEFAULT;
        std::map<uint8_t, std::string> m_labels_map;
        std::string m_mot_output_path;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        // Tracker config setters
        Builder &set_max_tracklets(uint16_t max_tracklets);
        Builder &set_max_missed_frames(uint8_t max_missed_frames);
        Builder &set_min_confirmed_frames(uint8_t min_confirmed_frames);
        Builder &set_aging_threshold(uint8_t aging_threshold);
        Builder &set_add_threshold(float add_threshold);
        Builder &set_association_threshold(float association_threshold);
        Builder &set_iou_weight(float iou_weight);
        Builder &set_class_aware_tracking(bool class_aware_tracking);
        Builder &set_enable_kalman_filter(bool enable_kalman_filter);
        Builder &set_position_std_weight(float position_std_weight);
        Builder &set_velocity_std_weight(float velocity_std_weight);
        Builder &set_smoothing_alpha(float smoothing_alpha);

        Builder &set_labels_map(std::map<uint8_t, std::string> labels_map);

        /**
         * @brief Set MOT Challenge CSV output path. Empty string (the default) disables output.
         *        Debug only — enabling this writes to the filesystem every frame and can impact performance.
         */
        Builder &set_mot_output_path(std::string path);

        /**
         * @brief Build and return a shared pointer to DetectionTrackerStage.
         * @throws std::runtime_error if required parameters are missing
         */
        std::shared_ptr<DetectionTrackerStage> buildptr() const;
    };

    static Builder create();
};

} // namespace hailo_analytics::pipeline::ai
