#include "detection_pipeline.hpp"

#include <stdint.h>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/frontend.hpp>
#include <tl/expected.hpp>
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <memory>

#include "common/common.hpp"
#include "resources/common/isp/common.hpp"
#include "detections_db.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/analytics/analytic_metadata_ws_sender.hpp"
#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_postprocess_tools/labels/hailo_yolov8n.hpp"
#include "common/logger_macros.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/cropping/tiling_stage.hpp"
#include "hailo_analytics/pipeline/routing/freeze_stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "pipeline/isp_blender.hpp"
#include "hailo_analytics/pipeline/ai/analytics_db_stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"

namespace ai_models = hailo_analytics::analytics::ai_models;

// Frontend AI stream name (matches camera configuration profiles)
static constexpr std::string_view DETECTION_AI_SINK = "detection_ai";

// Stage names
static constexpr std::string_view AGGREGATOR_STAGE = "aggregator";
static constexpr std::string_view TEE_STAGE = "vision_tee";
static constexpr std::string_view DETECTION_AI_PIPELINE = "detection_ai_pipeline";

static constexpr size_t DETECTION_CROP_EVERY_X_FRAMES = 2;
static constexpr size_t DETECTION_CROP_EVERY_X_FRAMES_SINGLE_TILE = 1;

using namespace hailo_analytics::pipeline::cropping;
using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::sources;
using namespace hailo_analytics::pipeline::ai;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

namespace tiling = hailo_analytics::analytics::tiling;
namespace detection = hailo_analytics::analytics::detection;

#define DETECTION_PIPELINE_SUPPORTED_PROFILES                                                                          \
    {ProfileType::Daylight, ProfileType::AiIspGen1, ProfileType::HighDynamicRange, ProfileType::AiIspGen2,             \
     ProfileType::AiIspGen3}

DetectionPipeline::DetectionPipeline(webserver::resources::ResourceRepository &resources, MediaLibraryPtr media_library,
                                     RTPConverterStage &webrtc_stage, Architecture platform, bool suppress_metadata_ws)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   DETECTION_PIPELINE_SUPPORTED_PROFILES),
      m_suppress_metadata_ws(suppress_metadata_ws)
{
}

std::string DetectionPipeline::pipeline_name() const
{
    return "Detection";
}

std::string DetectionPipeline::get_profile_name_by_type(ProfileType type) const
{
    switch (type)
    {
    case ProfileType::Daylight:
        return DETECTION_DAYLIGHT_PROFILE_NAME;
    case ProfileType::AiIspGen1:
        return DETECTION_AI_ISP_GEN1_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return DETECTION_HDR_PROFILE_NAME;
    case ProfileType::AiIspGen2:
        return DETECTION_AI_ISP_GEN2_PROFILE_NAME;
    case ProfileType::AiIspGen3:
        return DETECTION_AI_ISP_GEN3_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in Detection Pipeline");
    }
}

ProfileType DetectionPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == DETECTION_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == DETECTION_AI_ISP_GEN1_PROFILE_NAME)
        return ProfileType::AiIspGen1;
    else if (name == DETECTION_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == DETECTION_AI_ISP_GEN2_PROFILE_NAME)
        return ProfileType::AiIspGen2;
    else if (name == DETECTION_AI_ISP_GEN3_PROFILE_NAME)
        return ProfileType::AiIspGen3;
    else
        throw std::runtime_error("profile name not supported in Detection Pipeline");
}

void DetectionPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building detection pipeline");

    // Get AI stream dimensions from frontend
    auto output_streams = m_app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    int ai_width = 0, ai_height = 0;
    for (const auto &stream : output_streams.value())
    {
        if (stream.id == DETECTION_AI_SINK)
        {
            ai_width = stream.width;
            ai_height = stream.height;
            break;
        }
    }

    if (ai_width == 0 || ai_height == 0)
    {
        WEBSERVER_LOG_ERROR("Failed to get AI stream ({}) resolution from frontend", DETECTION_AI_SINK);
        throw std::runtime_error("Failed to get AI stream resolution from frontend");
    }

    WEBSERVER_LOG_INFO("AI stream ({}): {}x{}", DETECTION_AI_SINK, ai_width, ai_height);

    // 5-tile tiling by default; single-tile fallback for H15L imx678 lowlight_bayer.
    const bool single_tile = is_single_tile_mode();
    tiling::tiling_detection_config_t tiling_cfg;
    tiling_cfg.tiling_config.input_width = ai_width;
    tiling_cfg.tiling_config.input_height = ai_height;
    tiling_cfg.tiling_config.crop_every_x_frames = get_crop_every_x_frames();
    if (single_tile)
    {
        tiling_cfg.tiling_config.bbox_tiles = {{0.0, 0.0, 1.0, 1.0}};
        tiling_cfg.tiling_config.output_width = ai_width;
        tiling_cfg.tiling_config.output_height = ai_height;
        tiling_cfg.detection_config.ai_config.batch_size = 1;
        tiling_cfg.detection_config.ai_config.scheduler_threshold = 1;
    }
    else
    {
        tiling_cfg.detection_config.ai_config.batch_size = 5;
        tiling_cfg.detection_config.ai_config.scheduler_threshold = 5;
    }
    tiling_cfg.detection_config.ai_config.scheduler_timeout = std::chrono::milliseconds(50);

    ai_models::apply_to(ai_models::YOLOV8S, tiling_cfg.detection_config);

    // Enable detection tracker inside the tiling pipeline
    tiling_cfg.tracker_config.enabled = true;
    tiling_cfg.tracker_config.queue_size = 1;
    tiling_cfg.tracker_config.trace = false;
    tiling_cfg.tracker_config.labels_map = ::common::hailo_yolov8n;

    auto ai_pipeline_result =
        tiling::generate_tiling_detection_pipeline(std::string(DETECTION_AI_PIPELINE), tiling_cfg);
    if (!ai_pipeline_result.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to create tiling detection pipeline");
        throw std::runtime_error("Failed to create tiling detection pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr ai_pipeline = ai_pipeline_result.value();

    register_detections_db_config(ai_width, ai_height);
    auto detections_db_stage = build_detections_db_stage();

    // Skipped when m_suppress_metadata_ws is set, so the browser draws no bboxes.
    namespace ws_sender = hailo_analytics::analytics::analytic_metadata_ws_sender;
    hailo_analytics::pipeline::PipelinePtr ws_sender_pipeline;
    if (!m_suppress_metadata_ws)
    {
        ws_sender::analytic_metadata_ws_sender_config_t ws_cfg;
        ws_cfg.analytic_metadata_config.leaky = true;
        ws_sender_pipeline = ws_sender::generate_analytic_metadata_ws_sender_pipeline(
                                 std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE), ws_cfg)
                                 .value();
    }

    // Build webserver-specific stages
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);

    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(1)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();

    // Aggregator merges tiling AI results onto the 4K stream.
    // The 4K main inlet never carries EXPECTED_CROPS metadata (the inner tiling
    // pipeline emits one already-aggregated AI result per main frame), so we
    // must seed static_subframes here -- otherwise count_subframes() returns 0
    // and the merge is skipped, dropping all detections.
    std::string ai_output_stage_name = ai_pipeline->get_out_stage()->get_name();
    std::shared_ptr<AggregatorStage> aggregator_stage = AggregatorStageBuild::create()
                                                            .set_stage_name(std::string(AGGREGATOR_STAGE))
                                                            .set_main_inlet_name(DEFAULT_STREAM_4K_NAME)
                                                            .set_main_queue_size(3)
                                                            .set_main_leaky(true)
                                                            .set_sub_inlet_name(ai_output_stage_name)
                                                            .set_sub_queue_size(3)
                                                            .set_sub_leaky(false)
                                                            .set_multiscale_opt(!single_tile)
                                                            .set_static_subframes_opt(1)
                                                            .buildptr();

    // Assemble pipeline:
    // frontend(sink0) ──────────────────────────────→ aggregator(main)
    // frontend(detection_ai) → [tiling_detection+tracker] → aggregator(sub)
    // aggregator → metadata_tee → freeze → valve → encoder → tee → [udp, webrtc_sink]
    //                          ╰──→ [ws_metadata_sender] (skipped when suppressed)
    auto builder =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage("frontend", configure_frontend(), hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("freeze", m_app_resources->freeze_stage)
            .add_stage(ai_pipeline)
            .add_stage(std::string(AGGREGATOR_STAGE), aggregator_stage)
            .add_stage(detections_db_stage, hailo_analytics::pipeline::StageType::SINK)
            .add_stage("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME),
                       hailo_analytics::pipeline::StageType::SINK)
            .add_stage("tee", std::make_shared<TeeStage>(std::string(TEE_STAGE), 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage("main_sink", main_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .add_stage("metadata_tee", std::make_shared<TeeStage>(std::string("TEE_STAGE_2"), 2, false, false))
            // Frontend connections
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, std::string(AGGREGATOR_STAGE))
            .connect_frontend("frontend", std::string(DETECTION_AI_SINK), std::string(DETECTION_AI_PIPELINE))
            // AI pipeline (with tracker) output → aggregator
            .connect(std::string(DETECTION_AI_PIPELINE), std::string(AGGREGATOR_STAGE))
            // Aggregator → output
            .connect(std::string(AGGREGATOR_STAGE), "metadata_tee")
            .connect("metadata_tee", DETECTIONS_DB_STAGE)
            .connect("metadata_tee", "freeze")
            .connect("freeze", "valve")
            .connect("valve", "encoder")
            .connect("encoder", "tee")
            .connect("tee", "udp")
            .connect("tee", "main_sink");

    if (ws_sender_pipeline)
    {
        builder.add_stage(ws_sender_pipeline, hailo_analytics::pipeline::StageType::SINK)
            .connect("metadata_tee", std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE));
    }

    m_app_resources->pipeline = builder.build("DetectionPipeline");
}

void DetectionPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting DetectionPipeline");
    build_pipeline();
    BasePipeline::start();
}

bool DetectionPipeline::is_single_tile_mode() const
{
    return m_app_resources->platform == Architecture::Hailo15L &&
           webserver::common::get_sensor_type() == webserver::common::SensorModel::SENSOR_IMX678 &&
           m_current_profile_type == ProfileType::AiIspGen3;
}

size_t DetectionPipeline::get_crop_every_x_frames() const
{
    return is_single_tile_mode() ? DETECTION_CROP_EVERY_X_FRAMES_SINGLE_TILE : DETECTION_CROP_EVERY_X_FRAMES;
}

void DetectionPipeline::callback_handle_profile_switch(ResourceStateChangeNotification notif)
{
    BasePipeline::callback_handle_profile_switch(notif);

    size_t crop_every_x_frames = get_crop_every_x_frames();

    // Navigate into the AI sub-pipeline to reach the tiling stage
    auto ai_pipeline_stage = m_app_resources->pipeline->get_stage_by_name(std::string(DETECTION_AI_PIPELINE));
    auto ai_pipeline = std::dynamic_pointer_cast<hailo_analytics::pipeline::Pipeline>(ai_pipeline_stage);
    if (!ai_pipeline)
    {
        WEBSERVER_LOG_ERROR("Failed to get AI sub-pipeline for crop_every_x_frames update");
        return;
    }

    auto tiling_stage =
        std::dynamic_pointer_cast<TilingCropStage>(ai_pipeline->get_stage_by_name(std::string(tiling::TILING_STAGE)));
    if (!tiling_stage)
    {
        WEBSERVER_LOG_ERROR("Failed to get tiling stage for crop_every_x_frames update");
        return;
    }

    tiling_stage->set_crop_every_x_frames(crop_every_x_frames);
    WEBSERVER_LOG_INFO("Updated crop_every_x_frames to {} after profile switch", crop_every_x_frames);
}
