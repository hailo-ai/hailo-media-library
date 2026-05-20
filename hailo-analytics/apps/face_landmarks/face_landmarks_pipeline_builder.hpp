#pragma once

#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"

namespace face_landmarks_app
{

hailo_analytics::analytics::tiling::tiling_detection_config_t default_tiling_config();

hailo_analytics::analytics::face_landmarks::bbox_crop_landmarks_config_t default_landmarks_config();

} // namespace face_landmarks_app
