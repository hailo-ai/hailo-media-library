#pragma once

#include <atomic>
#include <memory>
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
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
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
inline constexpr std::string_view DETECTOR_LABEL_FILTER_STAGE = "detector_label_filter";
inline constexpr std::string_view DPM_ANALYTICS_DB_STAGE = "analytics_db_stage";
inline constexpr std::string_view DPM_ANALYTICS_DATA_ID = "semantic_segmentation";
inline constexpr std::string_view DPM_OVERFLOW_DETECTION_DATA_ID = "dpm_overflow_detection";

class DetectorLabelFilter : public hailo_analytics::pipeline::routing::CallbackStage
{
  public:
    DetectorLabelFilter(std::string name, size_t queue_size, bool leaky, std::vector<std::string> initial_labels);

    void set_labels(std::vector<std::string> labels);
    std::shared_ptr<const std::vector<std::string>> get_labels() const;

  private:
    std::shared_ptr<const std::vector<std::string>> m_labels;
};

struct detector_label_filter_config_t
{
    std::optional<std::vector<std::string>> labels;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;

    void merge_from(const detector_label_filter_config_t &other);
};

struct analytics_db_config_t
{
    std::optional<std::string> stage_name;
    std::optional<std::string> analytics_data_id;
    std::optional<std::string> overflow_analytics_data_id;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;

    void merge_from(const analytics_db_config_t &other);
};

struct full_dpm_analytics_config_t
{
    tiling::tiling_detection_config_t tiling_config;
    detector_label_filter_config_t detector_label_filter_config;
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
