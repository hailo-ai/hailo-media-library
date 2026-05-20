#include "face_landmarks_pipeline_builder.hpp"

namespace face_landmarks_app
{

namespace tiling = hailo_analytics::analytics::tiling;
namespace face_landmarks = hailo_analytics::analytics::face_landmarks;

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_tiling_pipeline(
    const std::string &name)
{
    auto cfg = tiling::base_config();
    cfg.detection_config.ai_config.hef_path = std::string(YOLO_HEF_FILE);
    cfg.detection_config.post_config.so_path = std::string(YOLO_POST_SO);
    cfg.detection_config.post_config.function_name = std::string(YOLO_FUNC_NAME);
    cfg.detection_config.post_config.config_path = std::string(YOLO_POST_CONF);
    cfg.tiling_config.queue_size = 2;
    cfg.aggregator_config.main_queue_size = 3;

    return tiling::generate_tiling_detection_pipeline(name, cfg);
}

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_landmarks_pipeline(
    const std::string &name)
{
    auto cfg = face_landmarks::base_config();
    cfg.bbox_crop_config.queue_size = 1;
    cfg.aggregator_config.main_queue_size = 3;
    cfg.aggregator_config.sub_queue_size = 20;
    cfg.landmarks_config.ai_config.queue_size = 20;
    cfg.landmarks_config.post_config.queue_size = 20;

    return face_landmarks::generate_bbox_landmarks_pipeline(name, cfg);
}

} // namespace face_landmarks_app
