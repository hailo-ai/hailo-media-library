#include "face_landmarks_pipeline_builder.hpp"

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <optional>
#include <string>

#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/analytics/detection.hpp"

namespace face_landmarks_app
{

namespace tiling = hailo_analytics::analytics::tiling;
namespace face_landmarks = hailo_analytics::analytics::face_landmarks;
namespace ai_models = hailo_analytics::analytics::ai_models;

static constexpr size_t TRACKER_QUEUE_SIZE_LOW_MEMORY = 1;

tiling::tiling_detection_config_t default_tiling_config()
{
    auto cfg = tiling::base_config();
    ai_models::apply_to(ai_models::YOLOV8N, cfg.detection_config);
    cfg.detection_config.ai_config.use_hailort_service = false;
    cfg.tiling_config.queue_size = 2;
    cfg.aggregator_config.main_queue_size = 3;

    cfg.tracker_config.enabled = true;
    cfg.tracker_config.queue_size = TRACKER_QUEUE_SIZE_LOW_MEMORY;
    cfg.tracker_config.labels_map = common::hailo_yolov8n;

    return cfg;
}

face_landmarks::bbox_crop_landmarks_config_t default_landmarks_config()
{
    auto cfg = face_landmarks::base_config();
    ai_models::apply_to(ai_models::FACE_LANDMARKS_LITE, cfg.landmarks_config);
    cfg.bbox_crop_config.queue_size = 1;
    cfg.bbox_crop_config.crop_every_x_frames = 2;
    cfg.aggregator_config.main_queue_size = 3;
    cfg.aggregator_config.sub_queue_size = 20;
    cfg.landmarks_config.ai_config.queue_size = 20;
    cfg.landmarks_config.ai_config.use_hailort_service = false;
    cfg.landmarks_config.post_config.queue_size = 20;

    return cfg;
}

} // namespace face_landmarks_app
