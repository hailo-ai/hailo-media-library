#include "license_plate_pipeline.hpp"

#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/frontend.hpp>
#include <tl/expected.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "common/common.hpp"
#include "resources/configs.hpp"
#include "resources/common/isp/common.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/analytics/license_plate_recognition.hpp"
#include "hailo_analytics/analytics/analytic_metadata_ws_sender.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "detections_db.hpp"
#include "lpr_pipeline_builder.hpp"
#include "common/logger_macros.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "pipeline/isp_blender.hpp"
#include "resources/common/events_utils.hpp"
#include "resources/common/resources.hpp"

static constexpr std::string_view DETECTION_AI_SINK = "detection_ai";

static constexpr std::string_view DETECTION_AI_PIPELINE = "detection_ai_pipeline";
static constexpr std::string_view LPR_PIPELINE = "lpr_pipeline";
static constexpr std::string_view AGGREGATOR_STAGE_NAME = "outer_aggregator";
static constexpr std::string_view METADATA_TEE = "metadata_tee";
static constexpr std::string_view OUTPUT_TEE = "vision_tee";

using namespace hailo_analytics::pipeline::cropping;
using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::sources;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

namespace tiling = hailo_analytics::analytics::tiling;
namespace lpr = hailo_analytics::analytics::license_plate_recognition;

#define LICENSE_PLATE_PIPELINE_SUPPORTED_PROFILES                                                                      \
    {ProfileType::Daylight, ProfileType::AiIspGen1, ProfileType::HighDynamicRange, ProfileType::AiIspGen2,             \
     ProfileType::AiIspGen3}

LicensePlatePipeline::LicensePlatePipeline(webserver::resources::ResourceRepository &resources,
                                           MediaLibraryPtr media_library, RTPConverterStage &webrtc_stage,
                                           Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   LICENSE_PLATE_PIPELINE_SUPPORTED_PROFILES)
{
}

std::string LicensePlatePipeline::pipeline_name() const
{
    return "LicensePlate";
}

bool LicensePlatePipeline::is_supported(webserver::resources::ResourceRepository &resources, Architecture /*platform*/)
{
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(resources.get(RESOURCE_CONFIG_MANAGER));
    // Supported on any platform/lighting combo for which the camera config
    // ships at least one *_LicensePlate profile.
    static constexpr const char *kProfiles[] = {
        LICENSE_PLATE_DAYLIGHT_PROFILE_NAME,    LICENSE_PLATE_AI_ISP_GEN1_PROFILE_NAME, LICENSE_PLATE_HDR_PROFILE_NAME,
        LICENSE_PLATE_AI_ISP_GEN2_PROFILE_NAME, LICENSE_PLATE_AI_ISP_GEN3_PROFILE_NAME,
    };
    for (const auto *name : kProfiles)
    {
        if (config->get_profile(name).has_value())
            return true;
    }
    WEBSERVER_LOG_INFO("License Plate Recognition not supported: no *_LicensePlate profile in media library config");
    return false;
}

std::string LicensePlatePipeline::get_profile_name_by_type(ProfileType type) const
{
    switch (type)
    {
    case ProfileType::Daylight:
        return LICENSE_PLATE_DAYLIGHT_PROFILE_NAME;
    case ProfileType::AiIspGen1:
        return LICENSE_PLATE_AI_ISP_GEN1_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return LICENSE_PLATE_HDR_PROFILE_NAME;
    case ProfileType::AiIspGen2:
        return LICENSE_PLATE_AI_ISP_GEN2_PROFILE_NAME;
    case ProfileType::AiIspGen3:
        return LICENSE_PLATE_AI_ISP_GEN3_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in LicensePlate Pipeline");
    }
}

ProfileType LicensePlatePipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == LICENSE_PLATE_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    if (name == LICENSE_PLATE_AI_ISP_GEN1_PROFILE_NAME)
        return ProfileType::AiIspGen1;
    if (name == LICENSE_PLATE_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    if (name == LICENSE_PLATE_AI_ISP_GEN2_PROFILE_NAME)
        return ProfileType::AiIspGen2;
    if (name == LICENSE_PLATE_AI_ISP_GEN3_PROFILE_NAME)
        return ProfileType::AiIspGen3;
    throw std::runtime_error("profile name not supported in LicensePlate Pipeline");
}

void LicensePlatePipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building license plate pipeline");

    auto output_streams = m_app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
        throw std::runtime_error("Failed to get frontend output streams");

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
        throw std::runtime_error("Failed to get AI stream resolution from frontend");

    WEBSERVER_LOG_INFO("AI stream ({}): {}x{}", DETECTION_AI_SINK, ai_width, ai_height);

    register_detections_db_config(ai_width, ai_height);
    auto detections_db_stage = build_detections_db_stage();

    const bool single_tile = m_app_resources->platform == Architecture::Hailo15L &&
                             webserver::common::get_sensor_type() == webserver::common::SensorModel::SENSOR_IMX678 &&
                             m_current_profile_type == ProfileType::AiIspGen3;

    auto tiling_cfg = lpr_app::default_tiling_config();
    tiling_cfg.tiling_config.input_width = ai_width;
    tiling_cfg.tiling_config.input_height = ai_height;
    if (single_tile)
    {
        tiling_cfg.tiling_config.bbox_tiles = {{0.0, 0.0, 1.0, 1.0}};
        tiling_cfg.tiling_config.output_width = ai_width;
        tiling_cfg.tiling_config.output_height = ai_height;
        tiling_cfg.detection_config.ai_config.batch_size = 1;
        tiling_cfg.detection_config.ai_config.scheduler_threshold = 1;
    }

    auto detection_pipeline_result =
        tiling::generate_tiling_detection_pipeline(std::string(DETECTION_AI_PIPELINE), tiling_cfg);
    if (!detection_pipeline_result.has_value())
        throw std::runtime_error("Failed to create tiling detection pipeline");
    auto detection_ai_pipeline = detection_pipeline_result.value();

    auto lpr_cfg = lpr_app::default_lpr_config();
    lpr_cfg.bbox_crop_config.input_width = ai_width;
    lpr_cfg.bbox_crop_config.input_height = ai_height;
    auto lpr_pipeline_result = lpr::generate_bbox_crop_ocr_pipeline(std::string(LPR_PIPELINE), lpr_cfg);
    if (!lpr_pipeline_result.has_value())
        throw std::runtime_error("Failed to create license plate OCR pipeline");
    auto lpr_pipeline = lpr_pipeline_result.value();

    namespace ws_sender = hailo_analytics::analytics::analytic_metadata_ws_sender;
    ws_sender::analytic_metadata_ws_sender_config_t ws_cfg;
    ws_cfg.analytic_metadata_config.leaky = true;
    auto ws_sender_pipeline = ws_sender::generate_analytic_metadata_ws_sender_pipeline(
                                  std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE), ws_cfg)
                                  .value();

    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);

    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(1)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();

    // The 4K main inlet has no EXPECTED_CROPS metadata, so seed static_subframes(1)
    // (matches DetectionPipeline). multiscale must be off in single-tile mode —
    // tile-relative coordinates would otherwise be rescaled twice.
    std::string lpr_out_stage_name = lpr_pipeline->get_out_stage()->get_name();
    std::shared_ptr<AggregatorStage> outer_aggregator = AggregatorStageBuild::create()
                                                            .set_stage_name(std::string(AGGREGATOR_STAGE_NAME))
                                                            .set_main_inlet_name(DEFAULT_STREAM_4K_NAME)
                                                            .set_main_queue_size(3)
                                                            .set_main_leaky(true)
                                                            .set_sub_inlet_name(lpr_out_stage_name)
                                                            .set_sub_queue_size(3)
                                                            .set_sub_leaky(false)
                                                            .set_multiscale_opt(!single_tile)
                                                            .set_static_subframes_opt(1)
                                                            .buildptr();

    m_app_resources->pipeline =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage("frontend", configure_frontend(), hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage(detection_ai_pipeline)
            .add_stage(lpr_pipeline)
            .add_stage(std::string(AGGREGATOR_STAGE_NAME), outer_aggregator)
            .add_stage(std::string(METADATA_TEE),
                       std::make_shared<TeeStage>(std::string(METADATA_TEE), 2, false, false))
            .add_stage(DETECTIONS_DB_STAGE, detections_db_stage, hailo_analytics::pipeline::StageType::SINK)
            .add_stage(ws_sender_pipeline, hailo_analytics::pipeline::StageType::SINK)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME),
                       hailo_analytics::pipeline::StageType::SINK)
            .add_stage(std::string(OUTPUT_TEE), std::make_shared<TeeStage>(std::string(OUTPUT_TEE), 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage("main_sink", main_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, std::string(AGGREGATOR_STAGE_NAME))
            .connect_frontend("frontend", std::string(DETECTION_AI_SINK), std::string(DETECTION_AI_PIPELINE))
            .connect(std::string(DETECTION_AI_PIPELINE), std::string(LPR_PIPELINE))
            .connect(std::string(DETECTION_AI_PIPELINE), DETECTIONS_DB_STAGE)
            .connect(std::string(LPR_PIPELINE), std::string(AGGREGATOR_STAGE_NAME))
            .connect(std::string(AGGREGATOR_STAGE_NAME), std::string(METADATA_TEE))
            .connect(std::string(METADATA_TEE), std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE))
            .connect(std::string(METADATA_TEE), "valve")
            .connect("valve", "encoder")
            .connect("encoder", std::string(OUTPUT_TEE))
            .connect(std::string(OUTPUT_TEE), "udp")
            .connect(std::string(OUTPUT_TEE), "main_sink")
            .build("LicensePlatePipeline");

    WEBSERVER_LOG_INFO("License plate pipeline built successfully");
}

void LicensePlatePipeline::start()
{
    WEBSERVER_LOG_INFO("Starting LicensePlatePipeline");
    build_pipeline();
    BasePipeline::start();
}
