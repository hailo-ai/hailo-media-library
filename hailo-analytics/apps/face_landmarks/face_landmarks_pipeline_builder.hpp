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

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_tiling_pipeline(
    const std::string &name);

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_landmarks_pipeline(
    const std::string &name);

} // namespace face_landmarks_app
