#include "face_landmarks_pipeline.hpp"

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <media_library/frontend.hpp>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "common/common.hpp"
#include "resources/configs.hpp"
#include "hailo_analytics/analytics/analytic_metadata_ws_sender.hpp"
#include "face_landmarks_pipeline_builder.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "detections_db.hpp"
#include "common/logger_macros.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "pipeline/isp_blender.hpp"
#include "resources/common/events_utils.hpp"
#include "resources/common/resources.hpp"

// Stream definitions
#define VISION_SINK "sink0"
#define SECONDARY_VISION_SINK "sink1"
#define AI_SINK "sink2"

// Sub-pipeline names
#define TILING_PIPELINE "tiling_pipeline"
#define LANDMARKS_PIPELINE "landmarks_pipeline"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::pipeline;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define FACE_LANDMARKS_PIPELINE_SUPPORTED_PROFILES                                                                     \
    {ProfileType::Daylight, ProfileType::AiIspGen1, ProfileType::HighDynamicRange, ProfileType::AiIspGen2,             \
     ProfileType::AiIspGen3}

FaceLandmarksPipeline::FaceLandmarksPipeline(webserver::resources::ResourceRepository &resources,
                                             MediaLibraryPtr media_library, RTPConverterStage &webrtc_stage,
                                             Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   FACE_LANDMARKS_PIPELINE_SUPPORTED_PROFILES)
{
}

std::string FaceLandmarksPipeline::pipeline_name() const
{
    return "FaceLandmarks";
}

bool FaceLandmarksPipeline::is_supported(webserver::resources::ResourceRepository &resources)
{
    // Verify Clip profile is available in media library
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(resources.get(RESOURCE_CONFIG_MANAGER));
    tl::expected<nlohmann::json, std::string> profile = config->get_profile(FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME);
    if (!profile.has_value())
    {
        WEBSERVER_LOG_WARNING("Face Landmarks not supported: Profile {} not found in media library configuration",
                              FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME);
        return false;
    }
    return true;
}

std::string FaceLandmarksPipeline::get_profile_name_by_type(ProfileType type) const
{
    switch (type)
    {
    case ProfileType::Daylight:
        return FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME;
    case ProfileType::AiIspGen1:
        return FACE_LANDMARKS_AI_ISP_GEN1_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return FACE_LANDMARKS_HDR_PROFILE_NAME;
    case ProfileType::AiIspGen2:
        return FACE_LANDMARKS_AI_ISP_GEN2_PROFILE_NAME;
    case ProfileType::AiIspGen3:
        return FACE_LANDMARKS_AI_ISP_GEN3_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in FaceLandmarks Pipeline");
    }
}

ProfileType FaceLandmarksPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == FACE_LANDMARKS_AI_ISP_GEN1_PROFILE_NAME)
        return ProfileType::AiIspGen1;
    else if (name == FACE_LANDMARKS_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == FACE_LANDMARKS_AI_ISP_GEN2_PROFILE_NAME)
        return ProfileType::AiIspGen2;
    else if (name == FACE_LANDMARKS_AI_ISP_GEN3_PROFILE_NAME)
        return ProfileType::AiIspGen3;
    else
        throw std::runtime_error("profile name not supported in FaceLandmarks Pipeline");
}

void FaceLandmarksPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building face landmarks pipeline");

    // AI sub-pipelines
    auto tiling_cfg = face_landmarks_app::default_tiling_config();
    auto tiling_pipeline =
        hailo_analytics::analytics::tiling::generate_tiling_detection_pipeline(TILING_PIPELINE, tiling_cfg).value();

    auto landmarks_cfg = face_landmarks_app::default_landmarks_config();
    auto landmarks_pipeline =
        hailo_analytics::analytics::face_landmarks::generate_bbox_landmarks_pipeline(LANDMARKS_PIPELINE, landmarks_cfg)
            .value();

    auto output_streams = m_app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }
    int ai_width = 0, ai_height = 0;
    for (const auto &stream : output_streams.value())
    {
        if (stream.id == AI_SINK)
        {
            ai_width = stream.width;
            ai_height = stream.height;
            break;
        }
    }
    if (ai_width == 0 || ai_height == 0)
    {
        WEBSERVER_LOG_ERROR("Failed to get AI stream ({}) resolution from frontend", AI_SINK);
        throw std::runtime_error("Failed to get AI stream resolution from frontend");
    }
    register_detections_db_config(ai_width, ai_height);
    auto detections_db_stage = build_detections_db_stage();

    namespace ws_sender = hailo_analytics::analytics::analytic_metadata_ws_sender;
    auto ws_sender_pipeline = ws_sender::generate_analytic_metadata_ws_sender_pipeline().value();

    // Vision stages
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);

    std::shared_ptr<EncoderStage> encoder_sink0 = configure_encoder_and_osd(VISION_SINK);
    std::shared_ptr<EncoderStage> encoder_sink1 = configure_encoder_and_osd(SECONDARY_VISION_SINK);
    std::shared_ptr<UdpStage> udp_sink0 = configure_udp(VISION_SINK);
    std::shared_ptr<UdpStage> udp_sink1 = configure_udp(SECONDARY_VISION_SINK);

    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(3)
            .set_leaky_opt(true)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();

    // Compose pipeline
    m_app_resources->pipeline =
        PipelineBuilder()
            .add_stage("frontend", configure_frontend(), StageType::SOURCE)
            .add_stage(tiling_pipeline)
            .add_stage(landmarks_pipeline)
            .add_stage(detections_db_stage, StageType::SINK)
            .add_stage(ws_sender_pipeline, StageType::SINK)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("encoder_sink0", encoder_sink0, StageType::SINK)
            .add_stage("encoder_sink1", encoder_sink1, StageType::SINK)
            .add_stage("tee_sink0", std::make_shared<TeeStage>("tee_sink0", 2, false, false))
            .add_stage("tee_sink1", std::make_shared<TeeStage>("tee_sink1", 2, false, false))
            .add_stage("udp_sink0", udp_sink0, StageType::SINK)
            .add_stage("udp_sink1", udp_sink1, StageType::SINK)
            .add_stage("main_sink", main_sink_stage, StageType::SINK)
            .connect_frontend("frontend", AI_SINK, TILING_PIPELINE)
            .connect_frontend("frontend", VISION_SINK, "valve")
            .connect_frontend("frontend", SECONDARY_VISION_SINK, "encoder_sink1")
            .connect(TILING_PIPELINE, LANDMARKS_PIPELINE)
            .connect(TILING_PIPELINE, DETECTIONS_DB_STAGE)
            .connect(LANDMARKS_PIPELINE, std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE))
            .connect("valve", "encoder_sink0")
            .connect("encoder_sink0", "tee_sink0")
            .connect("tee_sink0", "udp_sink0")
            .connect("tee_sink0", "main_sink")
            .connect("encoder_sink1", "tee_sink1")
            .connect("tee_sink1", "udp_sink1")
            .build("FaceLandmarksPipeline");

    WEBSERVER_LOG_INFO("Face landmarks pipeline built successfully");
}

void FaceLandmarksPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting FaceLandmarksPipeline");
    build_pipeline();
    BasePipeline::start();
}
