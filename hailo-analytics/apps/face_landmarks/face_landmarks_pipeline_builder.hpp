#pragma once

#include <cstddef>

#include <media_library/media_library_types.hpp>

#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"

namespace face_landmarks_app
{

hailo_analytics::analytics::tiling::tiling_detection_config_t default_tiling_config();

size_t get_max_crops(const config_profile_t &profile);

bool is_hailo15h_hdr(const config_profile_t &profile);

hailo_analytics::analytics::face_landmarks::bbox_crop_landmarks_config_t default_landmarks_config(
    const config_profile_t &profile);

} // namespace face_landmarks_app
