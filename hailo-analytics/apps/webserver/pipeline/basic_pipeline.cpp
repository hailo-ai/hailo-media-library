#include "basic_pipeline.hpp"

#include <memory>
#include <stdexcept>

#include "common/common.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "common/logger_macros.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "pipeline/isp_blender.hpp"
#include "resources/common/events_utils.hpp"

#define TEE_STAGE "vision_tee"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::sources;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define BASIC_PIPELINE_SUPPORTED_PROFILES                                                                              \
    {ProfileType::Daylight,  ProfileType::AiIspGen1, ProfileType::HighDynamicRange,                                    \
     ProfileType::AiIspGen2, ProfileType::AiIspGen3, ProfileType::AiIspGen3_1}

BasicPipeline::BasicPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                             RTPConverterStage &webrtc_stage, Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   BASIC_PIPELINE_SUPPORTED_PROFILES)
{
}

std::string BasicPipeline::pipeline_name() const
{
    return "Basic";
}

std::string BasicPipeline::get_profile_name_by_type(ProfileType type) const
{
    switch (type)
    {
    case ProfileType::Daylight:
        return BASIC_DAYLIGHT_PROFILE_NAME;
    case ProfileType::AiIspGen1:
        return BASIC_AI_ISP_GEN1_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return BASIC_HDR_PROFILE_NAME;
    case ProfileType::AiIspGen2:
        return BASIC_AI_ISP_GEN2_PROFILE_NAME;
    case ProfileType::AiIspGen3:
        return BASIC_AI_ISP_GEN3_PROFILE_NAME;
    case ProfileType::AiIspGen3_1:
        return BASIC_AI_ISP_GEN3_1_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in Basic Pipeline");
    }
}

ProfileType BasicPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == BASIC_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == BASIC_AI_ISP_GEN1_PROFILE_NAME)
        return ProfileType::AiIspGen1;
    else if (name == BASIC_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == BASIC_AI_ISP_GEN2_PROFILE_NAME)
        return ProfileType::AiIspGen2;
    else if (name == BASIC_AI_ISP_GEN3_PROFILE_NAME)
        return ProfileType::AiIspGen3;
    else if (name == BASIC_AI_ISP_GEN3_1_PROFILE_NAME)
        return ProfileType::AiIspGen3_1;
    else
        throw std::runtime_error("profile name not supported in Basic Pipeline");
}

void BasicPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building basic pipeline");
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(1)
            .set_leaky_opt(false)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();
    m_app_resources->pipeline =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage("frontend", configure_frontend(), hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME))
            .add_stage("tee", std::make_shared<TeeStage>(TEE_STAGE, 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage("main_sink", main_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, "valve")
            .connect("valve", "encoder")
            .connect("encoder", "tee")
            .connect("tee", "udp")
            .connect("tee", "main_sink")
            .build("BasicPipeline");
}

void BasicPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting BasicPipeline");
    build_pipeline();
    BasePipeline::start();
}
