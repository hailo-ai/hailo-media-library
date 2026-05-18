#pragma once

#include <string>
#include <string_view>
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/license_plate_recognition.hpp"

namespace lpr_app
{

enum class TrackingMode
{
    SLOW,
    FAST,
    BALANCED
};

TrackingMode tracking_mode_from_string(const std::string &mode_str);

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_tiling_pipeline(
    const std::string &name, TrackingMode mode = TrackingMode::BALANCED);

tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> build_ocr_pipeline(
    const std::string &name);

} // namespace lpr_app
