#pragma once

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
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::analytics::dpm_analytics
{

// Platform-specific max detection defaults
inline constexpr int DEFAULT_MAX_DETECTIONS_15H = 12;
inline constexpr int DEFAULT_MAX_DETECTIONS_15L = 6;
// Lowlight bayer: denoise NNC competes for DSP, reduce segmentation budget
inline constexpr int DEFAULT_MAX_DETECTIONS_15H_LOWLIGHT_BAYER = 4;
inline constexpr int DEFAULT_MAX_DETECTIONS_15L_LOWLIGHT_BAYER = 1;

// Full DPM analytics pipeline parameters
inline constexpr std::string_view FULL_DPM_ANALYTICS_PIPELINE = "full_dpm_analytics_pipeline";
inline constexpr std::string_view DETECTOR_LABEL_FILTER_STAGE = "detector_label_filter";
inline constexpr std::string_view DPM_SEGMENTOR_ANALYTICS_ID = "semantic_segmentation";

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

struct full_dpm_analytics_config_t
{
    tiling::tiling_detection_config_t tiling_config;
    detector_label_filter_config_t detector_label_filter_config;
    dynamic_privacy_mask::bbox_crop_segmentation_config_t dpm_config;
    std::optional<analytic_metadata_zmq_sender::analytic_metadata_zmq_sender_config_t> metadata_sender_config;

    // Smart encoder reads detections from AnalyticsDB::instance(); set false in apps with
    // no AnalyticsDB reader to skip the per-frame write.
    bool enable_detections_db_writer = true;

    void merge_from(const full_dpm_analytics_config_t &other);
};

full_dpm_analytics_config_t base_config();

full_dpm_analytics_config_t build_dpm_config(int ai_width, int ai_height, int max_detections,
                                             const std::vector<std::string> &segment_labels,
                                             const std::string &seg_hef_path = "");

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_full_dpm_analytics_pipeline(const std::string &pipeline_name = std::string(FULL_DPM_ANALYTICS_PIPELINE),
                                     const std::optional<full_dpm_analytics_config_t> &user_configs = std::nullopt);

} // namespace hailo_analytics::analytics::dpm_analytics
