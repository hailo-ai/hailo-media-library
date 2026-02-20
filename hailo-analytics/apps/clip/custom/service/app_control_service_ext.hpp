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
    ~AppControlServiceExt()
    {
    }

    tl::expected<bool, Error> set_clip_embedding_refresh_rate(size_t refresh_rate)
    {
        if (refresh_rate == 0)
        {
            HAILO_ANALYTICS_LOG_ERROR("Invalid refresh rate: {}. It must be greater than 0.", refresh_rate);
            return tl::unexpected(Error::INVALID_PARAMETER);
        }

        auto app_pipeline = m_app->get_pipeline();
        if (!app_pipeline)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get app pipeline.");
            return tl::unexpected(Error::INVALID_ACCESS);
        }

        auto pipeline = app_pipeline.value();
        auto traffic_control_stage =
            std::static_pointer_cast<hailo_analytics::pipeline::routing::TrackerTrafficCtrlStage>(
                pipeline->get_stage_by_name(app::stage::tracker_traffic_ctrl));
        if (!traffic_control_stage)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get tracker traffic control stage to set embedding refresh rate.");
            return tl::unexpected(Error::INVALID_ACCESS);
        }

        traffic_control_stage->set_unclassified_fps_to_block(refresh_rate);

        return true;
    }

    tl::expected<size_t, Error> get_clip_embedding_refresh_rate()
    {
        auto app_pipeline = m_app->get_pipeline();
        if (!app_pipeline)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get app pipeline.");
            return tl::unexpected(Error::INVALID_ACCESS);
        }

        auto pipeline = app_pipeline.value();
        auto traffic_control_stage =
            std::static_pointer_cast<hailo_analytics::pipeline::routing::TrackerTrafficCtrlStage>(
                pipeline->get_stage_by_name(app::stage::tracker_traffic_ctrl));
        if (!traffic_control_stage)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get tracker traffic control stage to get embedding refresh rate.");
            return tl::unexpected(Error::INVALID_ACCESS);
        }

        return traffic_control_stage->get_unclassified_fps_to_block();
    }
};
