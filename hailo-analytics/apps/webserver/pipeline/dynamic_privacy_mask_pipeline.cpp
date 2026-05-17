#include "dynamic_privacy_mask_pipeline.hpp"

#include "pipeline.hpp"
#include "common/common.hpp"

#include <algorithm>
#include <fmt/format.h>

// Analytics includes for DPM pipeline generation
#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"

// Pipeline stage names
constexpr std::string_view DPM_AI_PIPELINE = "dpm_ai_pipeline";
constexpr std::string_view TEE_STAGE = "tee_stage";
constexpr std::string_view WEBRTC_SINK = "webrtc_sink";

// DPM profiles use sink2 for AI (different from standard webserver profiles which use sink1)
constexpr std::string_view DPM_AI_SINK = "dpm_sink2";

// Import shared DPM constants
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15H;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15H_LOWLIGHT_BAYER;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15L;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15L_LOWLIGHT_BAYER;

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace hailo_analytics::pipeline;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define DPM_PIPELINE_SUPPORTED_PROFILES                                                                                \
    {ProfileType::Daylight, ProfileType::HighDynamicRange, ProfileType::LowlightBayer}

DynamicPrivacyMaskPipeline::DynamicPrivacyMaskPipeline(webserver::resources::ResourceRepository &resources,
                                                       MediaLibrary &media_library, RTPConverterStage &webrtc_stage,
                                                       Architecture platform)
    : BasePipeline(resources, media_library, webrtc_stage, platform, ProfileType::Daylight,
                   DPM_PIPELINE_SUPPORTED_PROFILES)
{
}

std::string DynamicPrivacyMaskPipeline::pipeline_name() const
{
    return "DynamicPrivacyMask";
}

std::string DynamicPrivacyMaskPipeline::get_profile_name_by_type(ProfileType type) const
{
    switch (type)
    {
    case ProfileType::Daylight:
        return DPM_DAYLIGHT_PROFILE_NAME;
    case ProfileType::Lowlight:
        return DPM_LOWLIGHT_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return DPM_HDR_PROFILE_NAME;
    case ProfileType::LowlightBayer:
        return DPM_LOWLIGHT_BAYER_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in DynamicPrivacyMask Pipeline");
    }
}

ProfileType DynamicPrivacyMaskPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == DPM_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == DPM_LOWLIGHT_PROFILE_NAME)
        return ProfileType::Lowlight;
    else if (name == DPM_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == DPM_LOWLIGHT_BAYER_PROFILE_NAME)
        return ProfileType::LowlightBayer;
    else
        throw std::runtime_error("profile name not supported in " + pipeline_name() + " Pipeline: " + name);
}

void DynamicPrivacyMaskPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building dynamic privacy mask pipeline");

    WEBSERVER_LOG_INFO("Using DPM resources: {}, {}", hailo_analytics::analytics::dpm_analytics::DEFAULT_YOLO_HEF,
                       hailo_analytics::analytics::dpm_analytics::DEFAULT_YOLO_FUNC_NAME);

    // Get stream dimensions from frontend
    auto output_streams = m_app_resources->media_library.m_frontend->get_outputs_streams();
    if (!output_streams.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    int ai_width = 0, ai_height = 0;
    for (const auto &stream : output_streams.value())
    {
        if (stream.id == DPM_AI_SINK)
        {
            ai_width = stream.width;
            ai_height = stream.height;
            break;
        }
    }

    if (ai_width == 0 || ai_height == 0)
    {
        WEBSERVER_LOG_ERROR("Failed to get AI stream ({}) resolution from frontend", DPM_AI_SINK);
        throw std::runtime_error("Failed to get AI stream resolution from frontend");
    }

    WEBSERVER_LOG_INFO("AI stream ({}): {}x{}", DPM_AI_SINK, ai_width, ai_height);

    // Create valve and freeze stages (for playback control)
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);

    // Create WebRTC sink stage
    std::shared_ptr<AppSinkStage> webrtc_sink_stage =
        AppSinkStageBuild::create()
            .set_stage_name(std::string(WEBRTC_SINK))
            .set_queue_size_opt(1)
            .set_leaky_opt(false)
            .set_process_func([&](hailo_analytics::pipeline::BufferPtr buf) { m_webrtc_stage.process(buf); })
            .buildptr();

    // Configure and generate the full DPM analytics pipeline
    int max_detections =
        (m_app_resources->platform == Architecture::Hailo15L) ? DEFAULT_MAX_DETECTIONS_15L : DEFAULT_MAX_DETECTIONS_15H;

    // Create shared segment_labels so the limiter callback reads them dynamically at runtime
    m_app_resources->segment_labels = std::make_shared<hailo_analytics::analytics::dpm_analytics::SharedLabels>();
    m_app_resources->segment_labels->store({"person", "vehicle"});

    // Create shared max_detections for runtime vision-mode adjustment
    m_shared_max_detections = std::make_shared<std::atomic<int>>(max_detections);

    auto dpm_ai_config = hailo_analytics::analytics::dpm_analytics::build_dpm_config(
        ai_width, ai_height, max_detections, {"person", "vehicle", "face"});
    dpm_ai_config.limiter_config.shared_segment_labels = m_app_resources->segment_labels;
    dpm_ai_config.limiter_config.shared_max_detections = m_shared_max_detections;

    auto dpm_ai_pipeline_status = hailo_analytics::analytics::dpm_analytics::generate_full_dpm_analytics_pipeline(
        std::string(DPM_AI_PIPELINE), dpm_ai_config);
    if (!dpm_ai_pipeline_status.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to create DPM analytics pipeline");
        throw std::runtime_error("Failed to create DPM analytics pipeline");
    }
    PipelinePtr dpm_ai_pipeline = dpm_ai_pipeline_status.value();

    // Build the complete pipeline
    // Vision path: Frontend → Freeze → Valve → Encoder → Tee → [UDP, WebRTC]
    // AI path: Frontend → DPM Analytics Pipeline (Tiling → Limiter → DPM → Analytics DB)
    m_app_resources->pipeline =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage("frontend", configure_frontend(), hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage("freeze", m_app_resources->freeze_stage)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("encoder", configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME))
            .add_stage(std::string(TEE_STAGE), std::make_shared<TeeStage>(std::string(TEE_STAGE), 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage(std::string(WEBRTC_SINK), webrtc_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .add_stage(dpm_ai_pipeline)
            // Vision path connections
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, "freeze")
            .connect("freeze", "valve")
            .connect("valve", "encoder")
            .connect("encoder", std::string(TEE_STAGE))
            .connect(std::string(TEE_STAGE), "udp")
            .connect(std::string(TEE_STAGE), std::string(WEBRTC_SINK))
            // AI path connection
            .connect_frontend("frontend", std::string(DPM_AI_SINK), std::string(DPM_AI_PIPELINE))
            .build("DynamicPrivacyMaskPipeline");

    WEBSERVER_LOG_INFO("Dynamic privacy mask pipeline built successfully");
}

void DynamicPrivacyMaskPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting DynamicPrivacyMaskPipeline");
    build_pipeline();
    BasePipeline::start();
}

static constexpr std::string_view DPM_CONFIG_ENDPOINT = "/dynamic_privacy_mask/config";
static const std::vector<std::string> LABEL_KEYS = {"person", "face", "vehicle", "license_plate"};

// Struct representing the DPM masking API surface for the frontend.
// Provides serialization to/from JSON and conversion to/from the media library profile config.
struct dpm_masking_config_t
{
    PrivacyMaskType mask_type = PrivacyMaskType::COLOR;
    int dilation_size = 0;
    int pixelization_size = 0;
    rgb_color_t color_value = {0, 0, 0};

    static dpm_masking_config_t from_profile(const config_profile_t &profile)
    {
        for (const auto &[stream_id, stream_config] : profile.encoded_output_streams)
        {
            if (!stream_config.masking.dynamic_privacy_mask_config.has_value())
                continue;

            return {
                .mask_type = stream_config.masking.mask_type,
                .dilation_size =
                    static_cast<int>(stream_config.masking.dynamic_privacy_mask_config.value().dilation_size),
                .pixelization_size = static_cast<int>(stream_config.masking.pixelization_size),
                .color_value = stream_config.masking.color_value,
            };
        }
        return {};
    }

    void apply_to_profile(config_profile_t &profile) const
    {
        for (auto &[stream_id, stream_config] : profile.encoded_output_streams)
        {
            if (!stream_config.masking.dynamic_privacy_mask_config.has_value())
                continue;

            stream_config.masking.mask_type = mask_type;
            stream_config.masking.pixelization_size = pixelization_size;
            stream_config.masking.color_value = color_value;
            stream_config.masking.dynamic_privacy_mask_config.value().dilation_size = dilation_size;
        }
    }
};

static void to_json(nlohmann::json &j, const dpm_masking_config_t &config)
{
    j["mask_type"] = (config.mask_type == PrivacyMaskType::PIXELIZATION) ? "PIXELIZATION" : "COLOR";
    j["dilation_size"] = config.dilation_size;
    j["pixelization_size"] = config.pixelization_size;
    j["color_value"] = nlohmann::json::array({config.color_value.r, config.color_value.g, config.color_value.b});
}

static void from_json(const nlohmann::json &j, dpm_masking_config_t &config)
{
    if (j.contains("mask_type"))
    {
        std::string type_str = j.at("mask_type").get<std::string>();
        config.mask_type = (type_str == "PIXELIZATION") ? PrivacyMaskType::PIXELIZATION : PrivacyMaskType::COLOR;
    }
    if (j.contains("dilation_size"))
        config.dilation_size = j.at("dilation_size").get<int>();
    if (j.contains("pixelization_size"))
        config.pixelization_size = j.at("pixelization_size").get<int>();
    if (j.contains("color_value"))
    {
        const auto &color = j.at("color_value");
        config.color_value = {color[0].get<uint>(), color[1].get<uint>(), color[2].get<uint>()};
    }
}

static nlohmann::json build_labels_response(const std::vector<std::string> &labels)
{
    nlohmann::json response;
    for (const auto &key : LABEL_KEYS)
    {
        response[key] = std::find(labels.begin(), labels.end(), key) != labels.end();
    }
    return response;
}

static void apply_masking_config(AppResources &app_resources, const nlohmann::json &masking_patch)
{
    auto expected_profile = app_resources.media_library.get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t profile = expected_profile.value();

    // Get current config, merge the patch, and apply back
    dpm_masking_config_t current = dpm_masking_config_t::from_profile(profile);
    nlohmann::json current_json = current;
    current_json.merge_patch(masking_patch);
    dpm_masking_config_t updated = current_json.get<dpm_masking_config_t>();
    updated.apply_to_profile(profile);

    media_library_return result = app_resources.media_library.set_override_parameters(profile);
    if (result != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to apply masking config change: {}", result);
        throw std::runtime_error("Failed to apply masking config change");
    }
    WEBSERVER_LOG_INFO("PATCH {} masking config updated via profile", DPM_CONFIG_ENDPOINT);
}

static void apply_segment_labels(hailo_analytics::analytics::dpm_analytics::SharedLabels &segment_labels,
                                 const nlohmann::json &j_body)
{
    auto current_labels = segment_labels.load();
    std::vector<std::string> new_labels;

    for (const auto &key : LABEL_KEYS)
    {
        bool enabled = j_body.contains(key)
                           ? j_body[key].get<bool>()
                           : std::find(current_labels->begin(), current_labels->end(), key) != current_labels->end();
        if (enabled)
        {
            new_labels.push_back(key);
        }
    }

    WEBSERVER_LOG_INFO("PATCH {} segment_labels updated to: [{}]", DPM_CONFIG_ENDPOINT, fmt::join(new_labels, ", "));
    segment_labels.store(std::move(new_labels));
}

void DynamicPrivacyMaskPipeline::register_endpoints()
{
    BasePipeline::register_endpoints();

    m_resources.m_srv.Get(std::string(DPM_CONFIG_ENDPOINT), std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", DPM_CONFIG_ENDPOINT);

                              auto expected_profile = m_app_resources->media_library.get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }

                              const auto &profile = expected_profile.value();
                              nlohmann::json response = dpm_masking_config_t::from_profile(profile);
                              response.merge_patch(build_labels_response(*m_app_resources->segment_labels->load()));
                              return response;
                          }));

    m_resources.m_srv.Patch(std::string(DPM_CONFIG_ENDPOINT), [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PATCH {} called with: {}", DPM_CONFIG_ENDPOINT, j_body.dump());

        static const std::vector<std::string> MASKING_KEYS = {"mask_type", "dilation_size", "pixelization_size",
                                                              "color_value"};
        nlohmann::json masking_patch;
        for (const auto &key : MASKING_KEYS)
        {
            if (j_body.contains(key))
                masking_patch[key] = j_body[key];
        }
        if (!masking_patch.empty())
        {
            apply_masking_config(*m_app_resources, masking_patch);
        }

        bool has_label_change = std::any_of(LABEL_KEYS.begin(), LABEL_KEYS.end(),
                                            [&](const std::string &key) { return j_body.contains(key); });
        if (has_label_change)
        {
            apply_segment_labels(*m_app_resources->segment_labels, j_body);
        }

        WEBSERVER_LOG_INFO("PATCH {} completed", DPM_CONFIG_ENDPOINT);
        return nlohmann::json();
    });
}

void DynamicPrivacyMaskPipeline::unregister_endpoints()
{
    WEBSERVER_LOG_INFO("Unregistering DynamicPrivacyMask Pipeline endpoints");
    m_resources.m_srv.Unregister(std::string(DPM_CONFIG_ENDPOINT));
    BasePipeline::unregister_endpoints();
}

void DynamicPrivacyMaskPipeline::callback_handle_profile_switch(ResourceStateChangeNotification notif)
{
    auto state = notif.getDirectResourceState<ProfileTypeState>();

    // Adjust max_detections per profile: lowlight bayer has denoise NNC competing for DSP,
    // so reduce segmentation budget.
    if (m_shared_max_detections)
    {
        int normal_max = (m_app_resources->platform == Architecture::Hailo15L) ? DEFAULT_MAX_DETECTIONS_15L
                                                                               : DEFAULT_MAX_DETECTIONS_15H;
        int lowlight_max = (m_app_resources->platform == Architecture::Hailo15L)
                               ? DEFAULT_MAX_DETECTIONS_15L_LOWLIGHT_BAYER
                               : DEFAULT_MAX_DETECTIONS_15H_LOWLIGHT_BAYER;
        int new_max = (state->value == ProfileType::LowlightBayer) ? lowlight_max : normal_max;
        m_shared_max_detections->store(new_max);
        WEBSERVER_LOG_INFO("DPM max_detections updated to {} for profile {}", new_max, static_cast<int>(state->value));
    }

    // Call base implementation for media library profile switch
    BasePipeline::callback_handle_profile_switch(notif);
}
