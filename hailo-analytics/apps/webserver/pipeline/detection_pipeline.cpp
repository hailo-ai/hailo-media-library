#include "pipeline.hpp"
#include "common/common.hpp"
#include <fstream>

// Stream definitions
#define DEFAULT_DETECTION_STREAM_640_640_NAME "detection_ai"

// Define platform-specific resources
// For Hailo15H
#define HAILO15_YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/hailo_yolov8s_384_640.hef"
#define HAILO15_YOLO_FUNC_NAME "hailo_yolov8s"
#define HAILO15_YOLO_POST_CONF "/home/root/apps/webserver/resources/configs/yolov5.json"

// For Hailo15L
#define HAILO15L_YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/hailo_yolov8s_384_640.hef"
#define HAILO15L_YOLO_FUNC_NAME "hailo_yolov8s"
#define HAILO15L_YOLO_POST_CONF "/home/root/apps/webserver/resources/configs/yolov5_personface.json"

// Stages
#define POST_STAGE "yolo_post"
#define TEE_STAGE "vision_tee"
#define DETECTION_AI_STAGE "yolo_detection"
#define AGGREGATOR_STAGE "aggregator"
#define WEBRTC_STAGE "webrtc_stage"
#define OVERLAY_STAGE "overlay"
#define LIGHTWEIGHT_TRACKER_STAGE "lightweight_tracker"

#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"

#define DETECTION_OVERLAYS_ENDPOINT "/detection/overlays"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define DETECTION_PIPELINE_SUPPORTED_PROFILES                                                                          \
    {ProfileType::Daylight, ProfileType::Lowlight, ProfileType::HighDynamicRange, ProfileType::LowlightBayer}

DetectionPipeline::DetectionPipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                                     std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   DETECTION_PIPELINE_SUPPORTED_PROFILES)
{
}

bool DetectionPipeline::should_draw_overlay()
{
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    return current_profile.application_settings.rotation.angle == rotation_angle_t::ROTATION_ANGLE_0;
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

    // Select the right HEF file and function name based on the platform
    std::string yolo_hef_file;
    std::string yolo_func_name;
    std::string yolo_post_conf;

    if (m_app_resources->platform == Architecture::Hailo15L)
    {
        yolo_hef_file = HAILO15L_YOLO_HEF_FILE;
        yolo_func_name = HAILO15L_YOLO_FUNC_NAME;
        yolo_post_conf = HAILO15L_YOLO_POST_CONF;
        WEBSERVER_LOG_INFO("Using Hailo15L resources: {}, {}", yolo_hef_file, yolo_func_name);
    }
    else
    {
        yolo_hef_file = HAILO15_YOLO_HEF_FILE;
        yolo_func_name = HAILO15_YOLO_FUNC_NAME;
        yolo_post_conf = HAILO15_YOLO_POST_CONF;
        WEBSERVER_LOG_INFO("Using Hailo15 resources: {}, {}", yolo_hef_file, yolo_func_name);
    }

    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);
    m_app_resources->overlay_stage =
        OverlayStageBuild::create().set_stage_name(OVERLAY_STAGE).set_skip_opt(!should_draw_overlay()).buildptr();
    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(1)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage->process(buf); })
            .buildptr();
    std::shared_ptr<LightweightTrackerStage> tracker_stage = LightweightTrackerStageBuild::create()
                                                                 .set_stage_name(LIGHTWEIGHT_TRACKER_STAGE)
                                                                 .set_queue_size_opt(1)
                                                                 .set_leaky_opt(false)
                                                                 .set_trace_opt(false)
                                                                 .set_classification_ids({1, 3})
                                                                 .set_add_tracking_id(false)
                                                                 .set_grace_period(4)
                                                                 .set_smooth_alpha(0.5f)
                                                                 .set_weighted_average_decay(0.4f)
                                                                 .set_copy_nested_objects(false, 1)
                                                                 .set_copy_nested_objects(true, 3)
                                                                 .buildptr();
    std::shared_ptr<AggregatorStage> aggregator_stage = AggregatorStageBuild::create()
                                                            .set_stage_name(AGGREGATOR_STAGE)
                                                            .set_static_subframes_opt(1)
                                                            .set_main_inlet_name(DEFAULT_STREAM_4K_NAME)
                                                            .set_main_queue_size(3)
                                                            .set_main_leaky(false)
                                                            .set_sub_inlet_name(POST_STAGE)
                                                            .set_sub_queue_size(3)
                                                            .set_sub_leaky(false)
                                                            .set_multiscale_opt(false)
                                                            .buildptr();

    std::shared_ptr<HailortAsyncStage> hailonet_stage = HailortAsyncStageBuild::create()
                                                            .set_stage_name(DETECTION_AI_STAGE)
                                                            .set_hef_path(yolo_hef_file)
                                                            .set_queue_size(5)
                                                            .set_output_pool_size(50)
                                                            .set_group_id("device0")
                                                            .set_batch_size(1)
                                                            .set_job_limit(10)
                                                            .set_scheduler_threshold_opt(1)
                                                            .set_dynamic_threshold_opt(false)
                                                            .set_scheduler_timeout_opt(std::chrono::milliseconds(50))
                                                            .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                            .buildptr();

    // Create pipeline with AI detection capabilities
    m_app_resources->pipeline =
        PipelineBuilder()
            .add_stage<FrontendStage>("frontend", configure_frontend(), StageType::SOURCE)
            .add_stage<ValveStage>("valve", m_app_resources->valve_stage)
            .add_stage<FreezeStage>("freeze", m_app_resources->freeze_stage)
            .add_stage<AggregatorStage>("aggregator", aggregator_stage)
            .add_stage<LightweightTrackerStage>(LIGHTWEIGHT_TRACKER_STAGE, tracker_stage)
            .add_stage<HailortAsyncStage>("hailonet", hailonet_stage)
            .add_stage<PostprocessStage>("post process",
                                         std::make_shared<PostprocessStage>(POST_STAGE, YOLO_POST_SO, yolo_func_name,
                                                                            yolo_post_conf, 5, false, false))
            .add_stage<OverlayStage>(OVERLAY_STAGE, m_app_resources->overlay_stage)
            .add_stage<EncoderStage>("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME), StageType::SINK)
            .add_stage<TeeStage>("tee", std::make_shared<TeeStage>("vision_tee", 2, false, false))
            .add_stage<UdpStage>("udp", configure_udp(DEFAULT_STREAM_4K_NAME), StageType::SINK)
            .add_stage<AppSinkStage>("main_sink", main_sink_stage, StageType::SINK)
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, "aggregator")
            .connect_frontend("frontend", DEFAULT_DETECTION_STREAM_640_640_NAME, "hailonet")
            .connect("hailonet", "post process")
            .connect("post process", "aggregator")
            .connect("aggregator", LIGHTWEIGHT_TRACKER_STAGE)
            .connect(LIGHTWEIGHT_TRACKER_STAGE, OVERLAY_STAGE)
            .connect(OVERLAY_STAGE, "freeze")
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

void DetectionPipeline::update_rotation(const std::string &rotation, config_profile_t &profile_config)
{
    // turn off detection overlay if rotation is not 0
    m_app_resources->overlay_stage->set_skip(rotation_string_map.at(rotation) != rotation_angle_t::ROTATION_ANGLE_0);

    BasePipeline::update_rotation(rotation, profile_config);
}

void DetectionPipeline::register_endpoints()
{
    BasePipeline::register_endpoints();
    m_resources->m_srv->Get(DETECTION_OVERLAYS_ENDPOINT, std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET {} called", DETECTION_OVERLAYS_ENDPOINT);

                                bool overlays_enabled = !m_app_resources->overlay_stage->get_skip();
                                nlohmann::json response;
                                response["enabled"] = overlays_enabled;
                                return response;
                            }));

    m_resources->m_srv->Post(DETECTION_OVERLAYS_ENDPOINT,
                             std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                                 WEBSERVER_LOG_INFO("POST {} called", DETECTION_OVERLAYS_ENDPOINT);

                                 bool enable_overlays = j_body["enabled"].get<bool>();
                                 m_app_resources->overlay_stage->set_skip(!enable_overlays);
                                 WEBSERVER_LOG_INFO("Detection overlays set to {}", enable_overlays);
                             }));
}

void DetectionPipeline::unregister_endpoints()
{
    WEBSERVER_LOG_INFO("Unregistering Detection Pipeline endpoints");

    m_resources->m_srv->Unregister(DETECTION_OVERLAYS_ENDPOINT);

    // Call parent's unregister function
    BasePipeline::unregister_endpoints();
}
