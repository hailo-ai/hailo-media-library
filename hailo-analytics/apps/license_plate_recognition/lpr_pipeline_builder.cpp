#include "lpr_pipeline_builder.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace lpr_app
{

namespace tiling = hailo_analytics::analytics::tiling;
namespace lpr = hailo_analytics::analytics::license_plate_recognition;

static constexpr std::string_view TRACKING_MODE_FAST = "fast";
static constexpr std::string_view TRACKING_MODE_SLOW = "slow";
static constexpr std::string_view TRACKING_MODE_BALANCED = "balanced";

struct TrackerConfig
{
    uint8_t max_missed_frames;
    float smoothing_alpha;
    float association_threshold;
};

static constexpr TrackerConfig FAST_CONFIG{1, 0.9f, 0.3f};
static constexpr TrackerConfig BALANCED_CONFIG{3, 0.7f, 0.3f};
static constexpr TrackerConfig SLOW_CONFIG{6, 0.5f, 0.4f};

static constexpr const TrackerConfig &get_tracker_config(TrackingMode mode)
{
    switch (mode)
    {
    case TrackingMode::FAST:
        return FAST_CONFIG;
    case TrackingMode::SLOW:
        return SLOW_CONFIG;
    case TrackingMode::BALANCED:
        return BALANCED_CONFIG;
    }
    __builtin_unreachable();
}

TrackingMode tracking_mode_from_string(const std::string &mode_str)
{
    std::string lower = mode_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == TRACKING_MODE_FAST)
    {
        return TrackingMode::FAST;
    }
    if (lower == TRACKING_MODE_SLOW)
    {
        return TrackingMode::SLOW;
    }
    if (lower == TRACKING_MODE_BALANCED)
    {
        return TrackingMode::BALANCED;
    }
    throw std::invalid_argument("Invalid tracking mode: '" + mode_str +
                                "'. Expected: " + std::string(TRACKING_MODE_SLOW) + ", " +
                                std::string(TRACKING_MODE_FAST) + ", or " + std::string(TRACKING_MODE_BALANCED));
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_tiling_pipeline(
    const std::string &name, TrackingMode mode)
{
    auto cfg = tiling::base_config();
    cfg.tiling_config.queue_size = 1;
    cfg.aggregator_config.main_queue_size = 3;

    // NMS class indices (0-based): 0=person, 1=vehicle, 2=face, 3=license_plate
    // Skip person and face — keep only vehicle and license_plate
    cfg.detection_config.ai_config.nms_classes_filter_mask = {false, true, false, true};

    // Enable detection tracker with mode-specific parameters
    const auto &tracker_params = get_tracker_config(mode);
    cfg.tracker_config.enabled = true;
    cfg.tracker_config.queue_size = 1;
    cfg.tracker_config.labels_map = common::hailo_yolov8n;
    cfg.tracker_config.max_missed_frames = tracker_params.max_missed_frames;
    cfg.tracker_config.smoothing_alpha = tracker_params.smoothing_alpha;
    cfg.tracker_config.association_threshold = tracker_params.association_threshold;

    return tiling::generate_tiling_detection_pipeline(name, cfg);
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_ocr_pipeline(
    const std::string &name)
{
    auto cfg = lpr::base_config();
    cfg.bbox_crop_config.input_width = 1920;
    cfg.bbox_crop_config.input_height = 1080;
    cfg.bbox_crop_config.queue_size = 1;
    cfg.aggregator_config.main_queue_size = 3;
    cfg.aggregator_config.sub_queue_size = 20;
    cfg.ocr_config.ai_config.queue_size = 20;
    cfg.ocr_config.post_config.queue_size = 20;

    return lpr::generate_bbox_crop_ocr_pipeline(name, cfg);
}

} // namespace lpr_app
