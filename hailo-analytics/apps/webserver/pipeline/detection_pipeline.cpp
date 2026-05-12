#include "detection_pipeline.hpp"
#include "common/common.hpp"
#include "resources/common/isp/common.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/ai/lightweight_tracker_stage.hpp"
#include "hailo_analytics/analytics/analytic_metadata_ws_sender.hpp"

// Frontend AI stream name (matches camera configuration profiles)
static constexpr std::string_view DETECTION_AI_SINK = "detection_ai";

// Platform-specific post-process config (Hailo15L uses person+face config)
static constexpr std::string_view HAILO15L_POST_CONF =
    "/home/root/apps/webserver/resources/configs/yolov5_personface.json";

// Stage names
static constexpr std::string_view AGGREGATOR_STAGE = "aggregator";
static constexpr std::string_view LIGHTWEIGHT_TRACKER_STAGE = "lightweight_tracker";
static constexpr std::string_view TEE_STAGE = "vision_tee";
static constexpr std::string_view DETECTION_AI_PIPELINE = "detection_ai_pipeline";
static constexpr std::string_view WS_SENDER_VALVE_STAGE = "ws_sender_valve";

// Tiling AI inference rate: 1 = every frame, 2 = every 2nd frame, etc.
static constexpr size_t DETECTION_CROP_EVERY_X_FRAMES = 1;
static constexpr size_t DETECTION_CROP_EVERY_X_FRAMES_LOWLIGHT_BAYER = 2;

static constexpr std::string_view DETECTION_METADATA_ENDPOINT = "/detection/metadata";

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
    {ProfileType::Daylight, ProfileType::Lowlight, ProfileType::HighDynamicRange, ProfileType::LowlightBayer}

DetectionPipeline::DetectionPipeline(webserver::resources::ResourceRepository &resources, MediaLibrary &media_library,
                                     RTPConverterStage &webrtc_stage, Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   DETECTION_PIPELINE_SUPPORTED_PROFILES)
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
    case ProfileType::Lowlight:
        return DETECTION_LOWLIGHT_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return DETECTION_HDR_PROFILE_NAME;
    case ProfileType::LowlightBayer:
        return DETECTION_LOWLIGHT_BAYER_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in Detection Pipeline");
    }
}

ProfileType DetectionPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == DETECTION_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == DETECTION_LOWLIGHT_PROFILE_NAME)
        return ProfileType::Lowlight;
    else if (name == DETECTION_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == DETECTION_LOWLIGHT_BAYER_PROFILE_NAME)
        return ProfileType::LowlightBayer;
    else
        throw std::runtime_error("profile name not supported in Detection Pipeline");
}

void DetectionPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building detection pipeline");

    // Get AI stream dimensions from frontend
    auto output_streams = m_app_resources->media_library.m_frontend->get_outputs_streams();
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

    // Configure tiling detection pipeline via analytics API generator
    // Single full-frame tile with no-op resize (input = output = stream dimensions)
    tiling::tiling_detection_config_t tiling_cfg;
    tiling_cfg.tiling_config.bbox_tiles = {{0.0, 0.0, 1.0, 1.0}};
    tiling_cfg.tiling_config.input_width = ai_width;
    tiling_cfg.tiling_config.input_height = ai_height;
    tiling_cfg.tiling_config.output_width = ai_width;
    tiling_cfg.tiling_config.output_height = ai_height;
    tiling_cfg.tiling_config.crop_every_x_frames = get_crop_every_x_frames();
    // Override detection defaults for webserver performance
    tiling_cfg.detection_config.ai_config.batch_size = 1;
    tiling_cfg.detection_config.ai_config.scheduler_threshold = 1;
    tiling_cfg.detection_config.ai_config.scheduler_timeout = std::chrono::milliseconds(50);

    // Platform-specific post-process config
    if (m_app_resources->platform == Architecture::Hailo15L)
    {
        tiling_cfg.detection_config.post_config.config_path = std::string(HAILO15L_POST_CONF);
        WEBSERVER_LOG_INFO("Using Hailo15L post-process config: {}", HAILO15L_POST_CONF);
    }

    auto ai_pipeline_result =
        tiling::generate_tiling_detection_pipeline(std::string(DETECTION_AI_PIPELINE), tiling_cfg);
    if (!ai_pipeline_result.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to create tiling detection pipeline");
        throw std::runtime_error("Failed to create tiling detection pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr ai_pipeline = ai_pipeline_result.value();

    // WebSocket metadata sender pipeline (disabled by default via valve)
    namespace ws_sender = hailo_analytics::analytics::analytic_metadata_ws_sender;
    ws_sender::analytic_metadata_ws_sender_config_t ws_cfg;
    ws_cfg.analytic_metadata_config.leaky = true;
    auto ws_sender_pipeline = ws_sender::generate_analytic_metadata_ws_sender_pipeline(
                                  std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE), ws_cfg)
                                  .value();

    // Lightweight tracker: smooths detections across frames
    std::shared_ptr<LightweightTrackerStage> tracker_stage = LightweightTrackerStageBuild::create()
                                                                 .set_stage_name(std::string(LIGHTWEIGHT_TRACKER_STAGE))
                                                                 .set_queue_size_opt(1)
                                                                 .set_leaky_opt(false)
                                                                 .set_trace_opt(false)
                                                                 .set_classification_ids({1, 3})
                                                                 .set_add_tracking_id(false)
                                                                 .set_grace_period(4)
                                                                 .set_smooth_alpha(0.5f)
                                                                 .set_weighted_average_decay(0.6f)
                                                                 .set_copy_nested_objects(false, 1)
                                                                 .set_copy_nested_objects(true, 3)
                                                                 .buildptr();

    // Build webserver-specific stages
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);

    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(1)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();

    // Aggregator merges AI results from tiling pipeline onto the 4K stream.
    std::string ai_output_stage_name = ai_pipeline->get_out_stage()->get_name();
    std::shared_ptr<AggregatorStage> aggregator_stage = AggregatorStageBuild::create()
                                                            .set_stage_name(std::string(AGGREGATOR_STAGE))
                                                            .set_static_subframes_opt(1)
                                                            .set_main_inlet_name(DEFAULT_STREAM_4K_NAME)
                                                            .set_main_queue_size(3)
                                                            .set_main_leaky(true)
                                                            .set_sub_inlet_name(ai_output_stage_name)
                                                            .set_sub_queue_size(3)
                                                            .set_sub_leaky(false)
                                                            .set_multiscale_opt(false)
                                                            .buildptr();

    // Assemble pipeline:
    // frontend(sink0) ──────────────────────────────→ aggregator(main)
    // frontend(detection_ai) → [tiling_detection] → aggregator(sub)
    // aggregator → tracker → metadata_tee → freeze → valve → encoder → tee → [udp, webrtc_sink]
    //                                    ╰──→ [ws_metadata_sender]
    m_app_resources->pipeline =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage("frontend", configure_frontend(), hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("freeze", m_app_resources->freeze_stage)
            .add_stage(ai_pipeline)
            .add_stage(std::string(AGGREGATOR_STAGE), aggregator_stage)
            .add_stage(std::string(LIGHTWEIGHT_TRACKER_STAGE), tracker_stage)
            .add_stage(ws_sender_pipeline, hailo_analytics::pipeline::StageType::SINK)
            .add_stage("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME),
                       hailo_analytics::pipeline::StageType::SINK)
            .add_stage("tee", std::make_shared<TeeStage>(std::string(TEE_STAGE), 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage("main_sink", main_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .add_stage("metadata_tee", std::make_shared<TeeStage>(std::string("TEE_STAGE_2"), 2, false, false))
            // Frontend connections
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, std::string(AGGREGATOR_STAGE))
            .connect_frontend("frontend", std::string(DETECTION_AI_SINK), std::string(DETECTION_AI_PIPELINE))
            // AI pipeline output → aggregator
            .connect(std::string(DETECTION_AI_PIPELINE), std::string(AGGREGATOR_STAGE))
            // Aggregator → tracker → output
            .connect(std::string(AGGREGATOR_STAGE), std::string(LIGHTWEIGHT_TRACKER_STAGE))
            .connect(std::string(LIGHTWEIGHT_TRACKER_STAGE), "metadata_tee")
            .connect("metadata_tee", std::string(ws_sender::ANALYTIC_METADATA_WS_SENDER_PIPELINE))
            .connect("metadata_tee", "freeze")
            .connect("freeze", "valve")
            .connect("valve", "encoder")
            .connect("encoder", "tee")
            .connect("tee", "udp")
            .connect("tee", "main_sink")
            .build("DetectionPipeline");
}

void DetectionPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting DetectionPipeline");
    build_pipeline();
    BasePipeline::start();
}

void DetectionPipeline::register_endpoints()
{
    BasePipeline::register_endpoints();

    m_resources.m_srv.Get(std::string(DETECTION_METADATA_ENDPOINT), std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", DETECTION_METADATA_ENDPOINT);
                              nlohmann::json response;
                              response["enabled"] = m_ws_sender_enabled;
                              return response;
                          }));

    m_resources.m_srv.Post(std::string(DETECTION_METADATA_ENDPOINT),
                           std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                               WEBSERVER_LOG_INFO("POST {} called", DETECTION_METADATA_ENDPOINT);

                               bool enable = j_body["enabled"].get<bool>();
                               m_ws_sender_enabled = enable;
                               m_ws_sender_valve->set_valve(enable);
                               WEBSERVER_LOG_INFO("Detection metadata sender set to {}", enable);
                           }));
}

void DetectionPipeline::unregister_endpoints()
{
    WEBSERVER_LOG_INFO("Unregistering Detection Pipeline endpoints");
    m_resources.m_srv.Unregister(std::string(DETECTION_METADATA_ENDPOINT));
    BasePipeline::unregister_endpoints();
}

size_t DetectionPipeline::get_crop_every_x_frames() const
{
    bool is_lowlight_bayer_imx678_15l =
        m_app_resources->platform == Architecture::Hailo15L &&
        webserver::common::get_sensor_type() == webserver::common::SensorModel::SENSOR_IMX678 &&
        m_current_profile_type == ProfileType::LowlightBayer;
    return is_lowlight_bayer_imx678_15l ? DETECTION_CROP_EVERY_X_FRAMES_LOWLIGHT_BAYER : DETECTION_CROP_EVERY_X_FRAMES;
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
