#pragma once

#include <tl/expected.hpp>
#include <string>

#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/license_plate_recognition.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace lpr_app
{

enum class TrackingMode
{
    SLOW,
    FAST,
    BALANCED
};

TrackingMode tracking_mode_from_string(const std::string &mode_str);

hailo_analytics::analytics::tiling::tiling_detection_config_t default_tiling_config(
    TrackingMode mode = TrackingMode::BALANCED);

hailo_analytics::analytics::license_plate_recognition::bbox_crop_ocr_config_t default_lpr_config();

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_tiling_pipeline(
    const std::string &name, TrackingMode mode = TrackingMode::BALANCED);

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_ocr_pipeline(
    const std::string &name);

} // namespace lpr_app
