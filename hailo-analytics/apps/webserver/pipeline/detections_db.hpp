#pragma once

#include "common/logger_macros.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"
#include "media_library/analytics_db.hpp"
#include "media_library/media_library_types.hpp"

#include <memory>
#include <string>

namespace webserver::pipeline
{

inline constexpr const char *DETECTIONS_DATA_ID = "detections";
inline constexpr const char *DETECTIONS_DB_STAGE = "detections_db";

inline void register_detections_db_config(int width, int height)
{
    detection_analytics_config_t detection_config;
    detection_config.analytics_data_id = DETECTIONS_DATA_ID;
    detection_config.scaling_mode = ScalingMode::STRETCH;
    detection_config.width = width;
    detection_config.height = height;
    detection_config.original_width_ratio = width;
    detection_config.original_height_ratio = height;
    detection_config.max_entries = 100;
    for (const auto &[id, name] : ::common::hailo_yolov8n)
        detection_config.labels.push_back({.label = name, .id = id});

    application_analytics_config_t application_config;
    application_config.detection_analytics_config[DETECTIONS_DATA_ID] = detection_config;
    AnalyticsDB::instance().add_configuration(application_config);

    WEBSERVER_LOG_INFO("Registered detections analytics config (id={}, {}x{}, labels={})", DETECTIONS_DATA_ID, width,
                       height, detection_config.labels.size());
}

inline std::shared_ptr<hailo_analytics::pipeline::ai::AnalyticsDBStage> build_detections_db_stage()
{
    return hailo_analytics::pipeline::ai::AnalyticsDBStageBuild::create()
        .set_stage_name(DETECTIONS_DB_STAGE)
        .set_queue_size(10)
        .set_leaky_opt(true)
        .set_analytics_data_id(DETECTIONS_DATA_ID)
        .set_type(AnalyticsType::DETECTION)
        .buildptr();
}

} // namespace webserver::pipeline
