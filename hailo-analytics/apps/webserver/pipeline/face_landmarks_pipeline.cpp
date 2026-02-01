#include "pipeline.hpp"
#include "common/common.hpp"
#include "ai_pipeline_builder.hpp"
#include <fstream>

// Stream definitions
#define VISION_SINK "sink0"
#define SECONDARY_VISION_SINK "sink1"
#define AI_SINK "sink2"

// Tiling Params
#define TILLING_INPUT_WIDTH 1920
#define TILLING_INPUT_HEIGHT 1080
#define TILLING_OUTPUT_WIDTH 640
#define TILLING_OUTPUT_HEIGHT 384

// Detection AI Params - platform-specific
// For Hailo15H
#define HAILO15_YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/hailo_yolov8n_384_640.hef"
#define HAILO15_YOLO_FUNC_NAME "hailo_yolov8n"
#define HAILO15_YOLO_POST_CONF "/home/root/apps/webserver/resources/configs/yolov5_personface.json"

// For Hailo15L (using same models for now)
#define HAILO15L_YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/hailo_yolov8n_384_640.hef"
#define HAILO15L_YOLO_FUNC_NAME "hailo_yolov8n"
#define HAILO15L_YOLO_POST_CONF "/home/root/apps/webserver/resources/configs/yolov5_personface.json"

#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"

// Landmarks AI Params
#define LANDMARKS_HEF_FILE "/home/root/apps/ai_example_app/resources/face_landmarks_lite.hef"
#define LANDMARKS_POST_SO "/usr/lib/hailo-post-processes/libmediapipe_post.so"
#define LANDMARKS_FUNC_NAME "facial_landmarks_nv12"

// Example: indices to draw for landmarks
const std::unordered_set<size_t> LANDMARKS_INDICES_EXAMPLE = {33, 468, 133, 362, 473, 263, 5, 4, 1};

// Tiles for detection
std::vector<HailoBBox> TILES = {
    {0.0, 0.0, 0.6, 0.6}, {0.4, 0, 0.6, 0.6}, {0, 0.4, 0.6, 0.6}, {0.4, 0.4, 0.6, 0.6}, {0.0, 0.0, 1.0, 1.0}};

#define WEBRTC_STAGE "webrtc_stage"
#define FACE_LANDMARKS_OVERLAYS_ENDPOINT "/face_landmarks/overlays"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::pipeline;
using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace ai_example_app;

#define FACE_LANDMARKS_PIPELINE_SUPPORTED_PROFILES                                                                     \
    {ProfileType::Daylight, ProfileType::Lowlight, ProfileType::HighDynamicRange, ProfileType::LowlightBayer}

FaceLandmarksPipeline::FaceLandmarksPipeline(WebserverResourceRepository resources,
                                             std::shared_ptr<MediaLibrary> media_library,
                                             std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   FACE_LANDMARKS_PIPELINE_SUPPORTED_PROFILES)
{
}

std::string FaceLandmarksPipeline::pipeline_name() const
{
    return "FaceLandmarks";
}

bool FaceLandmarksPipeline::is_supported(WebserverResourceRepository resources)
{
    // Verify Clip profile is available in media library
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(resources->get(RESOURCE_CONFIG_MANAGER));
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
    case ProfileType::Lowlight:
        return FACE_LANDMARKS_LOWLIGHT_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return FACE_LANDMARKS_HDR_PROFILE_NAME;
    case ProfileType::LowlightBayer:
        return FACE_LANDMARKS_LOWLIGHT_BAYER_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in FaceLandmarks Pipeline");
    }
}

ProfileType FaceLandmarksPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == FACE_LANDMARKS_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == FACE_LANDMARKS_LOWLIGHT_PROFILE_NAME)
        return ProfileType::Lowlight;
    else if (name == FACE_LANDMARKS_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == FACE_LANDMARKS_LOWLIGHT_BAYER_PROFILE_NAME)
        return ProfileType::LowlightBayer;
    else
        throw std::runtime_error("profile name not supported in FaceLandmarks Pipeline");
}

void FaceLandmarksPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building face landmarks pipeline");

    // Select the right HEF file and function name based on the platform
    std::string yolo_hef_file;
    std::string yolo_func_name;
    std::string yolo_post_conf;

    if (m_app_resources->platform == Architecture::Hailo15L)
    {
        yolo_hef_file = HAILO15L_YOLO_HEF_FILE;
        yolo_func_name = HAILO15L_YOLO_FUNC_NAME;
        yolo_post_conf = HAILO15L_YOLO_POST_CONF;
        WEBSERVER_LOG_INFO("Using Hailo15L resources for face landmarks: {}, {}", yolo_hef_file, yolo_func_name);
    }
    else
    {
        yolo_hef_file = HAILO15_YOLO_HEF_FILE;
        yolo_func_name = HAILO15_YOLO_FUNC_NAME;
        yolo_post_conf = HAILO15_YOLO_POST_CONF;
        WEBSERVER_LOG_INFO("Using Hailo15H resources for face landmarks: {}, {}", yolo_hef_file, yolo_func_name);
    }

    // Build pipeline using PipelineBuilder
    PipelineBuilder pip_builder;

    // Add frontend
    pip_builder.add_stage<FrontendStage>("frontend", configure_frontend(), StageType::SOURCE);

    // Get input resolution from frontend
    int bbox_crop_input_width = 0;
    int bbox_crop_input_height = 0;
    auto streams = m_app_resources->frontend->get_outputs_streams();
    if (streams.has_value())
    {
        for (const auto &stream : streams.value())
        {
            if (stream.id == VISION_SINK)
            {
                bbox_crop_input_width = stream.width;
                bbox_crop_input_height = stream.height;
                break;
            }
        }
    }

    if (bbox_crop_input_width == 0 || bbox_crop_input_height == 0)
    {
        WEBSERVER_LOG_ERROR("Failed to get input resolution from frontend");
        throw std::runtime_error("Failed to get input resolution from frontend");
    }

    // Configure AI Pipeline using shared builder
    AIPipelineConfig ai_config;
    ai_config.print_fps = false;
    ai_config.print_latency = false;
    ai_config.input_width = bbox_crop_input_width;
    ai_config.input_height = bbox_crop_input_height;
    ai_config.tiles = TILES;
    ai_config.yolo_hef_file = yolo_hef_file;
    ai_config.yolo_post_so = YOLO_POST_SO;
    ai_config.yolo_func_name = yolo_func_name;
    ai_config.yolo_post_conf = yolo_post_conf;
    ai_config.landmarks_hef_file = LANDMARKS_HEF_FILE;
    ai_config.landmarks_post_so = LANDMARKS_POST_SO;
    ai_config.landmarks_func_name = LANDMARKS_FUNC_NAME;
    ai_config.draw_overlay_sink0 = true;  // Enable overlay by default for main stream
    ai_config.draw_overlay_sink1 = false; // Disable overlay for secondary stream
    ai_config.full_landmarks = false;
    ai_config.landmark_indices_to_draw = LANDMARKS_INDICES_EXAMPLE;

    // Create AI pipeline stages using shared builder
    AIPipelineStages ai_stages = AIPipelineBuilder::create_ai_stages(ai_config);

    // Set callback function for the callback stage
    AIPipelineBuilder::set_callback_function(ai_stages, [](hailo_analytics::pipeline::BufferPtr data) {
        static int counter = 0;
        static const int threshold = 2; // Toggle every 2 calls
        counter = (counter + 1) % threshold;

        if (counter < threshold / 2)
        {
            CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(1);
            data->add_metadata(cropping_meta);
        }
        else
        {
            CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(0);
            data->add_metadata(cropping_meta);
        }
    });

    // Store overlay stages for later control
    m_app_resources->overlay_stage = ai_stages.overlay_sink0;

    // Create valve and freeze stages
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);

    // Create encoder and output stages
    std::shared_ptr<EncoderStage> encoder_sink0 = configure_encoder_and_osd(VISION_SINK);
    std::shared_ptr<EncoderStage> encoder_sink1 = configure_encoder_and_osd(SECONDARY_VISION_SINK);
    std::shared_ptr<UdpStage> udp_sink0 = configure_udp(VISION_SINK);
    std::shared_ptr<UdpStage> udp_sink1 = configure_udp(SECONDARY_VISION_SINK);

    // Create WebRTC sink stage
    std::shared_ptr<AppSinkStage> main_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name("main_sink")
            .set_queue_size_opt(3)
            .set_leaky_opt(true)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage->process(buf); })
            .buildptr();

    // Add AI pipeline stages using shared builder
    AIPipelineBuilder::add_stages_to_pipeline(pip_builder, ai_stages);

    // Add valve, freeze, and output stages
    pip_builder.add_stage<ValveStage>("valve", m_app_resources->valve_stage)
        .add_stage<FreezeStage>("freeze", m_app_resources->freeze_stage)
        .add_stage<EncoderStage>("encoder_sink0", encoder_sink0, StageType::SINK)
        .add_stage<EncoderStage>("encoder_sink1", encoder_sink1, StageType::SINK)
        .add_stage<TeeStage>("tee_sink0", std::make_shared<TeeStage>("tee_sink0", 2, false, false))
        .add_stage<TeeStage>("tee_sink1", std::make_shared<TeeStage>("tee_sink1", 2, false, false))
        .add_stage<UdpStage>("udp_sink0", udp_sink0, StageType::SINK)
        .add_stage<UdpStage>("udp_sink1", udp_sink1, StageType::SINK)
        .add_stage<AppSinkStage>("main_sink", main_sink_stage, StageType::SINK);

    // Connect frontend to AI pipeline
    pip_builder.connect_frontend("frontend", AI_SINK, ai_config.tiling_stage_name)
        .connect_frontend("frontend", VISION_SINK, ai_config.muxer_stage_name)
        .connect_frontend("frontend", SECONDARY_VISION_SINK, ai_config.muxer_stage_name);

    // Connect AI pipeline stages using shared builder
    AIPipelineBuilder::connect_ai_stages(pip_builder, ai_stages, ai_config);

    // Connect overlay outputs to freeze/valve and encoders
    pip_builder.connect(ai_config.overlay_stage_sink0_name, "freeze")
        .connect("freeze", "valve")
        .connect("valve", "encoder_sink0")
        .connect("encoder_sink0", "tee_sink0")
        .connect("tee_sink0", "udp_sink0")
        .connect("tee_sink0", "main_sink")
        .connect(ai_config.overlay_stage_sink1_name, "encoder_sink1")
        .connect("encoder_sink1", "tee_sink1")
        .connect("tee_sink1", "udp_sink1");

    // Build the pipeline
    m_app_resources->pipeline = pip_builder.build("FaceLandmarksPipeline");

    WEBSERVER_LOG_INFO("Face landmarks pipeline built successfully");
}

void FaceLandmarksPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting FaceLandmarksPipeline");
    build_pipeline();
    BasePipeline::start();
}

void FaceLandmarksPipeline::register_endpoints()
{
    BasePipeline::register_endpoints();

    // Register overlay control endpoint
    m_resources->m_srv->Get(FACE_LANDMARKS_OVERLAYS_ENDPOINT, std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET {} called", FACE_LANDMARKS_OVERLAYS_ENDPOINT);

                                bool overlays_enabled = !m_app_resources->overlay_stage->get_skip();
                                nlohmann::json response;
                                response["enabled"] = overlays_enabled;
                                return response;
                            }));

    m_resources->m_srv->Post(FACE_LANDMARKS_OVERLAYS_ENDPOINT,
                             std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                                 WEBSERVER_LOG_INFO("POST {} called", FACE_LANDMARKS_OVERLAYS_ENDPOINT);

                                 bool enable_overlays = j_body["enabled"].get<bool>();
                                 m_app_resources->overlay_stage->set_skip(!enable_overlays);
                                 WEBSERVER_LOG_INFO("Face landmarks overlays set to {}", enable_overlays);
                             }));
}

void FaceLandmarksPipeline::unregister_endpoints()
{
    WEBSERVER_LOG_INFO("Unregistering FaceLandmarks Pipeline endpoints");

    m_resources->m_srv->Unregister(FACE_LANDMARKS_OVERLAYS_ENDPOINT);

    // Call parent's unregister function
    BasePipeline::unregister_endpoints();
}
