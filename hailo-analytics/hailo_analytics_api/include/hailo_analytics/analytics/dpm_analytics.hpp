#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

#include <cstddef>
#include "tl/expected.hpp"

#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/dynamic_privacy_mask.hpp"
#include "hailo_analytics/analytics/analytic_metadata_zmq_sender.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/ai/lightweight_tracker_stage.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"

namespace hailo_analytics::analytics::dpm_analytics
{

// Resource path constants (shared between webserver and case study apps)
inline constexpr std::string_view DEFAULT_YOLO_HEF =
    "/home/root/apps/face_landmarks/resources/hailo_yolov8n_384_640.hef";
inline constexpr std::string_view DEFAULT_YOLO_POST_SO = "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so";
inline constexpr std::string_view DEFAULT_YOLO_FUNC_NAME = "hailo_yolov8n";
inline constexpr std::string_view DEFAULT_YOLO_POST_CONF = "/home/root/apps/webserver/resources/configs/yolov8n.json";
inline constexpr std::string_view DEFAULT_DPM_SEG_HEF =
    "/home/root/apps/dynamic_privacy_mask/resources/linknet_mbv1_ss_dpm_128.hef";

// Platform-specific max detection defaults
inline constexpr int DEFAULT_MAX_DETECTIONS_15H = 12;
inline constexpr int DEFAULT_MAX_DETECTIONS_15L = 6;
// Lowlight bayer: denoise NNC competes for DSP, reduce segmentation budget
inline constexpr int DEFAULT_MAX_DETECTIONS_15H_LOWLIGHT_BAYER = 4;
inline constexpr int DEFAULT_MAX_DETECTIONS_15L_LOWLIGHT_BAYER = 1;

// Full DPM analytics pipeline parameters
inline constexpr std::string_view FULL_DPM_ANALYTICS_PIPELINE = "full_dpm_analytics_pipeline";
inline constexpr std::string_view DETECTION_LIMITER_STAGE = "detection_limiter";
inline constexpr std::string_view DETECTION_LIMITER_PIPELINE = "detection_limiter_pipeline";
inline constexpr std::string_view DPM_ANALYTICS_DB_STAGE = "analytics_db_stage";
inline constexpr std::string_view DPM_ANALYTICS_DATA_ID = "semantic_segmentation";
inline constexpr std::string_view DPM_OVERFLOW_DETECTION_DATA_ID = "dpm_overflow_detection";
inline constexpr std::string_view TRACKER_STAGE = "dpm_tracker";
inline constexpr std::string_view TRACKER_PIPELINE = "dpm_tracker_pipeline";

/// Thread-safe container for dynamically-updated label lists.
/// Writer calls store() to publish a new label set; reader calls load() to get a snapshot.
struct SharedLabels
{
    void store(std::vector<std::string> labels)
    {
        std::atomic_store(&m_labels, std::make_shared<const std::vector<std::string>>(std::move(labels)));
    }

    std::shared_ptr<const std::vector<std::string>> load() const
    {
        return std::atomic_load(&m_labels);
    }

  private:
    std::shared_ptr<const std::vector<std::string>> m_labels = std::make_shared<const std::vector<std::string>>();
};

struct tracker_config_t
{
    std::optional<bool> enabled;
    std::optional<std::vector<int>> class_ids;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<int> grace_period;
    std::optional<float> iou_threshold;
    std::optional<size_t> history_size;

    void merge_from(const tracker_config_t &other);
};

struct detection_limiter_config_t
{
    std::optional<int> max_detections;
    std::optional<std::vector<std::string>> segment_labels;
    /// Labels that bypass segmentation but are always reported to overflow for bbox drawing
    /// (e.g. license_plate). Unlike segment_labels, these are not dynamically toggled.
    std::optional<std::vector<std::string>> overflow_only_labels;
    /// Enables runtime label updates from an external control plane (e.g. webserver PATCH endpoint).
    /// The limiter callback calls load() each frame; the webserver calls store() on PATCH.
    /// Takes priority over the static segment_labels field when set.
    std::shared_ptr<SharedLabels> shared_segment_labels;
    /// Enables runtime max_detections updates (e.g. on vision mode change).
    /// When set, the limiter reads from this atomic on every frame instead of the static max_detections.
    std::shared_ptr<std::atomic<int>> shared_max_detections;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> report_overflow_detections;
    std::optional<std::string> overflow_detection_analytics_data_id;

    void merge_from(const detection_limiter_config_t &other);
};

struct analytics_db_config_t
{
    std::optional<std::string> stage_name;
    std::optional<std::string> analytics_data_id;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;

    void merge_from(const analytics_db_config_t &other);
};

struct full_dpm_analytics_config_t
{
    tiling::tiling_detection_config_t tiling_config;
    tracker_config_t tracker_config;
    detection_limiter_config_t limiter_config;
    dynamic_privacy_mask::bbox_crop_segmentation_config_t dpm_config;
    analytics_db_config_t analytics_db_config;
    std::optional<analytic_metadata_zmq_sender::analytic_metadata_zmq_sender_config_t> metadata_sender_config;

    void merge_from(const full_dpm_analytics_config_t &other);
};

full_dpm_analytics_config_t base_config();

full_dpm_analytics_config_t build_dpm_config(int ai_width, int ai_height, int max_detections,
                                             const std::vector<std::string> &segment_labels,
                                             const std::string &seg_hef_path = std::string(DEFAULT_DPM_SEG_HEF));

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_full_dpm_analytics_pipeline(const std::string &pipeline_name = std::string(FULL_DPM_ANALYTICS_PIPELINE),
                                     const std::optional<full_dpm_analytics_config_t> &user_configs = std::nullopt);

} // namespace hailo_analytics::analytics::dpm_analytics
