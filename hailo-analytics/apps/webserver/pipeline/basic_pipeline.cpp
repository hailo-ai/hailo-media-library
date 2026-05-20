#include "basic_pipeline.hpp"
#include "common/common.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"

#define TEE_STAGE "vision_tee"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::sources;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define BASIC_PIPELINE_SUPPORTED_PROFILES                                                                              \
    {ProfileType::Daylight, ProfileType::Lowlight, ProfileType::HighDynamicRange, ProfileType::LowlightBayer,          \
     ProfileType::DenoiseHdr}

BasicPipeline::BasicPipeline(webserver::resources::ResourceRepository &resources, MediaLibrary &media_library,
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
    case ProfileType::Lowlight:
        return BASIC_LOWLIGHT_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return BASIC_HDR_PROFILE_NAME;
    case ProfileType::LowlightBayer:
        return BASIC_LOWLIGHT_BAYER_PROFILE_NAME;
    case ProfileType::DenoiseHdr:
        return BASIC_DENOISE_HDR_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in Basic Pipeline");
    }
}

ProfileType BasicPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == BASIC_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == BASIC_LOWLIGHT_PROFILE_NAME)
        return ProfileType::Lowlight;
    else if (name == BASIC_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == BASIC_LOWLIGHT_BAYER_PROFILE_NAME)
        return ProfileType::LowlightBayer;
    else if (name == BASIC_DENOISE_HDR_PROFILE_NAME)
        return ProfileType::DenoiseHdr;
    else
        throw std::runtime_error("profile name not supported in Basic Pipeline");
}

void BasicPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building basic pipeline");
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);
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
            .add_stage("freeze", m_app_resources->freeze_stage)
            .add_stage("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME))
            .add_stage("tee", std::make_shared<TeeStage>(TEE_STAGE, 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage("main_sink", main_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, "freeze")
            .connect("freeze", "valve")
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
