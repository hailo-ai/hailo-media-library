#include "dynamic_privacy_mask_pipeline.hpp"

#include <fmt/format.h>
#include <stddef.h>
#include <media_library/media_library_api_types.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <tl/expected.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "pipeline.hpp"
#include "common/common.hpp"
// Analytics includes for DPM pipeline generation
#include "hailo_analytics/analytics/ai_models_config.hpp"
#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/muxing/bundle_streams_stage.hpp"
#include "hailo_analytics/pipeline/muxing/split_streams_stage.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "detections_db.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "hailo_analytics/analytics/dynamic_privacy_mask.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/overlay/overlay_stage.hpp"
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "pipeline/isp_blender.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"

namespace ai_models = hailo_analytics::analytics::ai_models;

// Pipeline stage names
constexpr std::string_view DPM_AI_PIPELINE = "dpm_ai_pipeline";
constexpr std::string_view TEE_STAGE = "tee_stage";
constexpr std::string_view WEBRTC_SINK = "webrtc_sink";
constexpr std::string_view BUNDLE_STAGE = "bundle";
constexpr std::string_view SPLIT_STAGE = "split";
constexpr std::string_view AI_TERMINAL_SINK = "ai_terminal_sink";

// DPM profiles use sink2 for AI (different from standard webserver profiles which use sink1)
constexpr std::string_view DPM_AI_SINK = "dpm_sink2";

constexpr std::string_view LABEL_PERSON = "person";
constexpr std::string_view LABEL_FACE = "face";
constexpr std::string_view LABEL_VEHICLE = "vehicle";
constexpr std::string_view LABEL_LICENSE_PLATE = "license_plate";

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
    {ProfileType::Daylight, ProfileType::HighDynamicRange, ProfileType::AiIspGen2, ProfileType::AiIspGen3}

DynamicPrivacyMaskPipeline::DynamicPrivacyMaskPipeline(webserver::resources::ResourceRepository &resources,
                                                       MediaLibraryPtr media_library, RTPConverterStage &webrtc_stage,
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
    case ProfileType::AiIspGen1:
        return DPM_AI_ISP_GEN1_PROFILE_NAME;
    case ProfileType::HighDynamicRange:
        return DPM_HDR_PROFILE_NAME;
    case ProfileType::AiIspGen2:
        return DPM_AI_ISP_GEN2_PROFILE_NAME;
    case ProfileType::AiIspGen3:
        return DPM_AI_ISP_GEN3_PROFILE_NAME;
    default:
        throw std::runtime_error("profile type not supported in DynamicPrivacyMask Pipeline");
    }
}

ProfileType DynamicPrivacyMaskPipeline::get_profile_type_by_name(const std::string &name) const
{
    if (name == DPM_DAYLIGHT_PROFILE_NAME)
        return ProfileType::Daylight;
    else if (name == DPM_AI_ISP_GEN1_PROFILE_NAME)
        return ProfileType::AiIspGen1;
    else if (name == DPM_HDR_PROFILE_NAME)
        return ProfileType::HighDynamicRange;
    else if (name == DPM_AI_ISP_GEN2_PROFILE_NAME)
        return ProfileType::AiIspGen2;
    else if (name == DPM_AI_ISP_GEN3_PROFILE_NAME)
        return ProfileType::AiIspGen3;
    else
        throw std::runtime_error("profile name not supported in " + pipeline_name() + " Pipeline: " + name);
}

void DynamicPrivacyMaskPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building dynamic privacy mask pipeline");

    WEBSERVER_LOG_INFO("Using DPM resources: {}, {}", ai_models::YOLOV8N.hef_relative,
                       ai_models::YOLOV8N.post_function_name);

    // Get stream dimensions from frontend
    auto output_streams = m_app_resources->media_library->get_frontend_output_streams();
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

    // Create valve stage (for playback control)
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);

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

    std::vector<std::string> initial_labels{std::string(LABEL_PERSON), std::string(LABEL_VEHICLE)};
    auto dpm_ai_config = hailo_analytics::analytics::dpm_analytics::build_dpm_config(ai_width, ai_height,
                                                                                     max_detections, initial_labels);
    dpm_ai_config.detector_label_filter_config.labels = initial_labels;

    auto dpm_ai_pipeline_status = hailo_analytics::analytics::dpm_analytics::generate_full_dpm_analytics_pipeline(
        std::string(DPM_AI_PIPELINE), dpm_ai_config);
    if (!dpm_ai_pipeline_status.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to create DPM analytics pipeline");
        throw std::runtime_error("Failed to create DPM analytics pipeline");
    }
    PipelinePtr dpm_ai_pipeline = dpm_ai_pipeline_status.value();

    m_detector_filter = std::dynamic_pointer_cast<hailo_analytics::analytics::dpm_analytics::DetectorLabelFilter>(
        dpm_ai_pipeline->get_stage_by_name(
            std::string(hailo_analytics::analytics::dpm_analytics::DETECTOR_LABEL_FILTER_STAGE)));
    auto dpm_sub = std::dynamic_pointer_cast<hailo_analytics::pipeline::Pipeline>(dpm_ai_pipeline->get_stage_by_name(
        std::string(hailo_analytics::analytics::dynamic_privacy_mask::BBOX_CROP_SEGMENTATION_PIPELINE)));
    if (dpm_sub)
        m_segmentor = std::dynamic_pointer_cast<hailo_analytics::pipeline::cropping::BBoxCropStage>(
            dpm_sub->get_stage_by_name(std::string(hailo_analytics::analytics::dynamic_privacy_mask::SEGMENTOR_STAGE)));
    if (!m_detector_filter || !m_segmentor)
    {
        WEBSERVER_LOG_ERROR("Failed to recover DPM runtime-mutable stage handles by name");
        throw std::runtime_error("Failed to recover DPM runtime-mutable stage handles");
    }

    apply_label_set(initial_labels);

    register_detections_db_config(ai_width, ai_height);

    auto bundle = hailo_analytics::pipeline::muxing::BundleStreamsStageBuild::create()
                      .set_stage_name(std::string(BUNDLE_STAGE))
                      .set_carrier_stream_id(std::string(DPM_AI_SINK))
                      .set_passenger_stream_ids({std::string(DEFAULT_STREAM_4K_NAME)})
                      .set_queue_size_opt(1)
                      .buildptr();

    auto split = hailo_analytics::pipeline::muxing::SplitStreamsStageBuild::create()
                     .set_stage_name(std::string(SPLIT_STAGE))
                     .set_carrier_stream_id(std::string(DPM_AI_SINK))
                     .set_propagate_roi_opt(true)
                     .set_queue_size_opt(1)
                     .buildptr();

    auto encoder_4k = configure_encoder_and_osd(DEFAULT_STREAM_4K_NAME);
    encoder_4k->set_attach_analytics_metadata(true);

    auto ai_terminal_sink = AppSinkStageBuild::create()
                                .set_stage_name(std::string(AI_TERMINAL_SINK))
                                .set_queue_size_opt(1)
                                .set_leaky_opt(true)
                                .set_process_func([](hailo_analytics::pipeline::BufferPtr) {})
                                .buildptr();

    m_app_resources->pipeline =
        hailo_analytics::pipeline::PipelineBuilder()
            .add_stage("frontend", configure_frontend(), hailo_analytics::pipeline::StageType::SOURCE)
            .add_stage(std::string(BUNDLE_STAGE), bundle)
            .add_stage(dpm_ai_pipeline)
            .add_stage(std::string(SPLIT_STAGE), split)
            .add_stage("valve", m_app_resources->valve_stage)
            .add_stage("encoder", encoder_4k)
            .add_stage(std::string(TEE_STAGE), std::make_shared<TeeStage>(std::string(TEE_STAGE), 2, false, false))
            .add_stage("udp", configure_udp(DEFAULT_STREAM_4K_NAME), hailo_analytics::pipeline::StageType::SINK)
            .add_stage(std::string(WEBRTC_SINK), webrtc_sink_stage, hailo_analytics::pipeline::StageType::SINK)
            .add_stage(std::string(AI_TERMINAL_SINK), ai_terminal_sink, hailo_analytics::pipeline::StageType::SINK)
            .connect_frontend("frontend", DEFAULT_STREAM_4K_NAME, std::string(BUNDLE_STAGE))
            .connect_frontend("frontend", std::string(DPM_AI_SINK), std::string(BUNDLE_STAGE))
            .connect(std::string(BUNDLE_STAGE), std::string(DPM_AI_PIPELINE))
            .connect(std::string(DPM_AI_PIPELINE), std::string(SPLIT_STAGE))
            .connect(std::string(SPLIT_STAGE), std::string(DEFAULT_STREAM_4K_NAME), "valve")
            .connect(std::string(SPLIT_STAGE), std::string(DPM_AI_SINK), std::string(AI_TERMINAL_SINK))
            .connect("valve", "encoder")
            .connect("encoder", std::string(TEE_STAGE))
            .connect(std::string(TEE_STAGE), "udp")
            .connect(std::string(TEE_STAGE), std::string(WEBRTC_SINK))
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
static constexpr std::array<std::string_view, 4> LABEL_KEYS = {LABEL_PERSON, LABEL_FACE, LABEL_VEHICLE,
                                                               LABEL_LICENSE_PLATE};

void DynamicPrivacyMaskPipeline::apply_label_set(std::vector<std::string> labels)
{
    m_user_labels = labels;

    auto detector_targets = labels;
    auto person_it = std::find(detector_targets.begin(), detector_targets.end(), LABEL_PERSON);
    if (person_it != detector_targets.end())
    {
        auto face_it = std::find(detector_targets.begin(), detector_targets.end(), LABEL_FACE);
        if (face_it != detector_targets.end())
            detector_targets.erase(face_it);
    }

    std::vector<std::string> segmentor_targets;
    for (const auto &lbl : detector_targets)
    {
        if (lbl != LABEL_FACE && lbl != LABEL_LICENSE_PLATE)
            segmentor_targets.push_back(lbl);
    }

    if (m_detector_filter)
        m_detector_filter->set_labels(detector_targets);
    if (m_segmentor)
        m_segmentor->set_labels(std::move(segmentor_targets));
}

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
        response[std::string(key)] = std::find(labels.begin(), labels.end(), key) != labels.end();
    }
    return response;
}

static void apply_masking_config(AppResources &app_resources, const nlohmann::json &masking_patch)
{
    auto expected_profile = app_resources.media_library->get_current_profile();
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

    media_library_return result = app_resources.media_library->set_override_parameters(profile);
    if (result != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to apply masking config change: {}", result);
        throw std::runtime_error("Failed to apply masking config change");
    }
    WEBSERVER_LOG_INFO("PATCH {} masking config updated via profile", DPM_CONFIG_ENDPOINT);
}

static std::vector<std::string> compute_new_segment_labels(const std::vector<std::string> &current_labels,
                                                           const nlohmann::json &j_body)
{
    std::vector<std::string> new_labels;
    for (const auto &key : LABEL_KEYS)
    {
        std::string key_str(key);
        bool enabled = j_body.contains(key_str)
                           ? j_body[key_str].get<bool>()
                           : std::find(current_labels.begin(), current_labels.end(), key) != current_labels.end();
        if (enabled)
            new_labels.emplace_back(key);
    }
    return new_labels;
}

void DynamicPrivacyMaskPipeline::register_endpoints()
{
    BasePipeline::register_endpoints();

    m_resources.m_srv.Get(std::string(DPM_CONFIG_ENDPOINT), std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET {} called", DPM_CONFIG_ENDPOINT);

                              auto expected_profile = m_app_resources->media_library->get_current_profile();
                              if (!expected_profile.has_value())
                              {
                                  WEBSERVER_LOG_ERROR("Failed to get current profile");
                                  throw std::runtime_error("Failed to get current profile");
                              }

                              const auto &profile = expected_profile.value();
                              nlohmann::json response = dpm_masking_config_t::from_profile(profile);
                              response.merge_patch(build_labels_response(m_user_labels));
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
                                            [&](std::string_view key) { return j_body.contains(std::string(key)); });
        if (has_label_change)
        {
            auto new_labels = compute_new_segment_labels(m_user_labels, j_body);
            apply_label_set(new_labels);
            WEBSERVER_LOG_INFO("PATCH {} segment_labels updated to: [{}]", DPM_CONFIG_ENDPOINT,
                               fmt::join(new_labels, ", "));
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
    if (m_segmentor)
    {
        int normal_max = (m_app_resources->platform == Architecture::Hailo15L) ? DEFAULT_MAX_DETECTIONS_15L
                                                                               : DEFAULT_MAX_DETECTIONS_15H;
        int lowlight_max = (m_app_resources->platform == Architecture::Hailo15L)
                               ? DEFAULT_MAX_DETECTIONS_15L_LOWLIGHT_BAYER
                               : DEFAULT_MAX_DETECTIONS_15H_LOWLIGHT_BAYER;
        const bool is_bayer_denoise = state->value == ProfileType::AiIspGen2 || state->value == ProfileType::AiIspGen3;
        int new_max = is_bayer_denoise ? lowlight_max : normal_max;
        m_segmentor->set_max_crops(static_cast<size_t>(new_max));
        WEBSERVER_LOG_INFO("DPM max_crops updated to {} for profile {}", new_max, static_cast<int>(state->value));
    }

    // Call base implementation for media library profile switch
    BasePipeline::callback_handle_profile_switch(notif);
}
