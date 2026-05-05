#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <sys/statvfs.h>
#include <tl/expected.hpp>
#include "common_utils.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "hailo_analytics/pipeline/routing/tracker_traffic_ctrl_stage.hpp"
#include "clip_pipeline_ai_defines.hpp"

class AppControlServiceExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension
{
  public:
    // Error types
    enum Error
    {
        INVALID_ACCESS,
        INVALID_PARAMETER,
    };

    // Constructor
    AppControlServiceExt() = default;

    // Destructor
    ~AppControlServiceExt() = default;

    tl::expected<bool, Error> set_clip_embedding_refresh_rate(size_t refresh_rate);

    tl::expected<size_t, Error> get_clip_embedding_refresh_rate();
};
