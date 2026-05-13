#pragma once

#include <string>
#include <string_view>
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"

namespace face_landmarks_app
{

// Person+face YOLO detection model paths
inline constexpr std::string_view YOLO_HEF_FILE = "/home/root/apps/face_landmarks/resources/hailo_yolov8n_384_640.hef";
inline constexpr std::string_view YOLO_POST_SO = "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so";
inline constexpr std::string_view YOLO_FUNC_NAME = "hailo_yolov8n";
inline constexpr std::string_view YOLO_POST_CONF = "/home/root/apps/webserver/resources/configs/yolov5_personface.json";

hailo_analytics::analytics::tiling::tiling_detection_config_t default_tiling_config();

hailo_analytics::analytics::face_landmarks::bbox_crop_landmarks_config_t default_landmarks_config();

} // namespace face_landmarks_app
