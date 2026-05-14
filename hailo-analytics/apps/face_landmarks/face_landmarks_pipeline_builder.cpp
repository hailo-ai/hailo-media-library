#include "face_landmarks_pipeline_builder.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"

namespace face_landmarks_app
{

namespace tiling = hailo_analytics::analytics::tiling;
namespace face_landmarks = hailo_analytics::analytics::face_landmarks;

tiling::tiling_detection_config_t default_tiling_config()
{
    auto cfg = tiling::base_config();
    cfg.detection_config.ai_config.hef_path = std::string(YOLO_HEF_FILE);
    cfg.detection_config.ai_config.use_hailort_service = false;
    cfg.detection_config.post_config.so_path = std::string(YOLO_POST_SO);
    cfg.detection_config.post_config.function_name = std::string(YOLO_FUNC_NAME);
    cfg.detection_config.post_config.config_path = std::string(YOLO_POST_CONF);
    cfg.tiling_config.queue_size = 2;
    cfg.aggregator_config.main_queue_size = 3;

    cfg.tracker_config.enabled = true;
    cfg.tracker_config.labels_map = common::hailo_yolov8n;

    return cfg;
}

face_landmarks::bbox_crop_landmarks_config_t default_landmarks_config()
{
    auto cfg = face_landmarks::base_config();
    cfg.bbox_crop_config.queue_size = 1;
    cfg.aggregator_config.main_queue_size = 3;
    cfg.aggregator_config.sub_queue_size = 20;
    cfg.landmarks_config.ai_config.queue_size = 20;
    cfg.landmarks_config.ai_config.use_hailort_service = false;
    cfg.landmarks_config.post_config.queue_size = 20;

    return cfg;
}

} // namespace face_landmarks_app
