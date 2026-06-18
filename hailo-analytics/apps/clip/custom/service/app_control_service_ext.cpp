#include "app_control_service_ext.hpp"

#include <memory>

#include "clip_pipeline_ai_defines.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/routing/tracker_traffic_ctrl_stage.hpp"

tl::expected<bool, AppControlServiceExt::Error> AppControlServiceExt::set_clip_embedding_refresh_rate(
    size_t refresh_rate)
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
    auto traffic_control_stage = std::static_pointer_cast<hailo_analytics::pipeline::routing::TrackerTrafficCtrlStage>(
        pipeline->get_stage_by_name(app::stage::tracker_traffic_ctrl));
    if (!traffic_control_stage)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get tracker traffic control stage to set embedding refresh rate.");
        return tl::unexpected(Error::INVALID_ACCESS);
    }

    traffic_control_stage->set_unclassified_fps_to_block(refresh_rate);

    return true;
}

tl::expected<size_t, AppControlServiceExt::Error> AppControlServiceExt::get_clip_embedding_refresh_rate()
{
    auto app_pipeline = m_app->get_pipeline();
    if (!app_pipeline)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get app pipeline.");
        return tl::unexpected(Error::INVALID_ACCESS);
    }

    auto pipeline = app_pipeline.value();
    auto traffic_control_stage = std::static_pointer_cast<hailo_analytics::pipeline::routing::TrackerTrafficCtrlStage>(
        pipeline->get_stage_by_name(app::stage::tracker_traffic_ctrl));
    if (!traffic_control_stage)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get tracker traffic control stage to get embedding refresh rate.");
        return tl::unexpected(Error::INVALID_ACCESS);
    }

    return traffic_control_stage->get_unclassified_fps_to_block();
}
