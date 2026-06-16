#include "pipeline_factory.hpp"

#include <media_library/media_library_types.hpp>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <optional>

#include "common/common.hpp"
#include "config_path_utils.hpp"
#include "detections_action.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "media_library/analytics_db.hpp"
#include "resources/configs.hpp"
#include "resources/encoder.hpp"
#include "resources/webrtc.hpp"
#include "pipeline/basic_pipeline.hpp"
#include "pipeline/clip_pipeline.hpp"
#include "pipeline/detection_pipeline.hpp"
#include "pipeline/dynamic_privacy_mask_pipeline.hpp"
#include "pipeline/face_landmarks_pipeline.hpp"
#include "pipeline/license_plate_pipeline.hpp"
#include "pipeline/profile_manager_pipeline.hpp"
#include "resources/common/events_utils.hpp"
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"
#include "clip_pipeline_ai.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "pipeline/isp_blender.hpp"
#include "pipeline/pipeline.hpp"
#include "resources/common/event_bus.hpp"
#include "resources/common/resources.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace hailo_analytics::pipeline;

#define EVENT_SUBSCRIBER_ID "pipeline_factory_subscriber"

PipelineFactory::PipelineFactory(webserver::resources::ResourceRepository &resources, Architecture platform,
                                 const pipeline_t &initial_pipeline_type)
    : m_resources(resources), m_platform(platform)
{
    WEBSERVER_LOG_INFO("Initializing PipelineFactory with initial type: {}", static_cast<int>(initial_pipeline_type));
    register_endpoints();
    m_current_pipeline_type = initial_pipeline_type;
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(m_resources.get(RESOURCE_CONFIG_MANAGER));
    std::string medialib_config_string = apps::utils::resolve_relative_refs(
        config->get_current_medialib_config().dump(), config->get_medialib_config_path());
    tl::expected<std::shared_ptr<MediaLibrary>, media_library_return> media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return;
    }
    m_media_library = media_lib_expected.value();
    if (m_media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }

    std::shared_ptr<webserver::resources::WebRtcResource> webrtc_resource =
        std::static_pointer_cast<WebRtcResource>(m_resources.get(RESOURCE_WEBRTC));
    m_webrtc_stage = RTPConverterStageBuild::create()
                         .set_stage_name("webrtc_stage")
                         .set_session_name("main")
                         .set_rtp_receiver(webrtc_resource)
                         .set_leaky_opt(true)
                         .set_trace_opt(false)
                         .buildptr();
    m_webrtc_stage->init();

    m_resources.m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, EventType::PROFILE_UPDATE_REQUEST, EventPriority::EVENT_PRIORITY_MEDIUM,
        [this](ResourceStateChangeNotification /*notification*/) {
            WEBSERVER_LOG_INFO("Received PROFILE_UPDATE_REQUEST notification");
            if (!m_current_pipeline)
            {
                WEBSERVER_LOG_ERROR("No current pipeline to update profile");
                throw std::runtime_error("No current pipeline to update profile");
            }

            auto expected_profile = m_media_library->get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            ProfileType profile_type = m_current_pipeline->get_current_profile();
            std::string profile_name = m_current_pipeline->get_profile_name_by_type(profile_type);
            m_resources.m_event_bus->notify(
                EventType::PROFILE_UPDATE,
                std::make_shared<ProfileState>(ProfileStateData{current_profile, profile_type, profile_name,
                                                                m_current_pipeline->get_supported_profiles()}));
        });

    m_resources.m_event_bus->subscribe(EVENT_SUBSCRIBER_ID, EventType::RESET_CONFIG,
                                       EventPriority::EVENT_PRIORITY_VERY_HIGH,
                                       [this, initial_pipeline_type](ResourceStateChangeNotification /*notification*/) {
                                           WEBSERVER_LOG_INFO("Received RESET_CONFIG notification");
                                           std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                                           if (switch_pipeline(initial_pipeline_type) != AppStatus::SUCCESS)
                                           {
                                               WEBSERVER_LOG_ERROR("Failed to reset current pipeline of type: {}",
                                                                   static_cast<int>(initial_pipeline_type));
                                               throw std::runtime_error("Failed to reset current pipeline");
                                           }
                                       });

    // Subscribed at HIGH priority — runs before BasePipeline's MEDIUM-priority encoder
    // reconfigure, so an auto-switch tears down the old pipeline before a wasted reconfigure.
    m_resources.m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, EventType::CHANGED_RESOURCE_ENCODER, EventPriority::EVENT_PRIORITY_HIGH,
        [this](ResourceStateChangeNotification notification) {
            auto state = notification.getResourceStateFromBase<EncoderResource::EncoderResourceState>();
            if (!state || !state->value.smart_encoder.has_value())
                return;
            const auto &smart_encoder = *state->value.smart_encoder;
            const bool requires_detections = smart_encoder.global_enable && smart_encoder.analytics_labels.any();
            handle_detections_requirement(requires_detections);
        });

    m_supported_pipelines = {pipeline_t::Basic, pipeline_t::Detection, pipeline_t::DynamicPrivacyMask};
    if (ClipPipeline::is_supported(m_resources))
    {
        m_supported_pipelines.push_back(pipeline_t::CLIP);
    }
    if (FaceLandmarksPipeline::is_supported(m_resources))
    {
        m_supported_pipelines.push_back(pipeline_t::FaceLandmarks);
    }
    if (LicensePlatePipeline::is_supported(m_resources, m_platform))
    {
        m_supported_pipelines.push_back(pipeline_t::LicensePlate);
    }
    for (const auto &pipeline_type : m_supported_pipelines)
    {
        nlohmann::json supported_pipeline_json;
        supported_pipeline_json["pipeline_type"] = pipeline_type;
        WEBSERVER_LOG_INFO("Supported pipeline: {}", supported_pipeline_json.dump());
    }
    WEBSERVER_LOG_INFO("PipelineFactory initialized successfully");
}

PipelineFactory::~PipelineFactory()
{
    m_resources.m_event_bus->unsubscribe_all(EVENT_SUBSCRIBER_ID);
    // Stop the current pipeline if it exists
    if (m_current_pipeline)
    {
        m_current_pipeline->stop();
    }
    // Stop the shared WebRTC stage on full shutdown (not during pipeline switches).
    // The webrtc stage is shared across pipelines and must only be stopped here.
    if (m_webrtc_stage)
    {
        m_webrtc_stage->stop();
    }
    WEBSERVER_LOG_DEBUG("PipelineFactory destroyed");
}

BasePipeline *PipelineFactory::get_current_pipeline()
{
    std::lock_guard<std::mutex> lock(m_pipeline_mutex);
    if (!m_current_pipeline)
    {
        if (switch_pipeline(m_current_pipeline_type, false) != AppStatus::SUCCESS)
        {
            WEBSERVER_LOG_ERROR("Failed to switch to initial pipeline of type: {}",
                                static_cast<int>(m_current_pipeline_type));
            throw std::runtime_error("Failed to switch to initial pipeline");
        }
    }

    return m_current_pipeline.get();
}

pipeline_t PipelineFactory::get_current_pipeline_type()
{
    std::lock_guard<std::mutex> lock(m_pipeline_mutex);
    return m_current_pipeline_type;
}

std::unique_ptr<BasePipeline> PipelineFactory::create_pipeline(const pipeline_t &pipeline_type)
{
    WEBSERVER_LOG_INFO("Creating pipeline of type: {}", static_cast<int>(pipeline_type));

    if (pipeline_type == pipeline_t::Basic)
    {
        return std::make_unique<BasicPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::Detection)
    {
        return std::make_unique<DetectionPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform,
                                                   /*suppress_metadata_ws=*/false);
    }
    else if (pipeline_type == pipeline_t::DetectionInternal)
    {
        return std::make_unique<DetectionPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform,
                                                   /*suppress_metadata_ws=*/true);
    }
    else if (pipeline_type == pipeline_t::CLIP)
    {
        return std::make_unique<ClipPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::ProfileManager)
    {
        return std::make_unique<ProfileManagerPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::FaceLandmarks)
    {
        return std::make_unique<FaceLandmarksPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::DynamicPrivacyMask)
    {
        return std::make_unique<DynamicPrivacyMaskPipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::LicensePlate)
    {
        return std::make_unique<LicensePlatePipeline>(m_resources, m_media_library, *m_webrtc_stage, m_platform);
    }
    else
    {
        WEBSERVER_LOG_ERROR("Unknown pipeline type requested: {}", static_cast<int>(pipeline_type));
        throw std::runtime_error("Unknown pipeline type: " + std::to_string(static_cast<int>(pipeline_type)));
    }
}

std::vector<pipeline_t> PipelineFactory::get_supported_pipeline_types() const
{
    return m_supported_pipelines;
}

AppStatus PipelineFactory::set_override_persistent_settings(bool value)
{
    if (!m_media_library)
    {
        WEBSERVER_LOG_ERROR("Cannot set override_persistent_settings: media_library is null");
        return AppStatus::UNINITIALIZED;
    }

    m_media_library->set_override_persistent_settings(value);
    WEBSERVER_LOG_INFO("Set override_persistent_settings to: {}", value);
    return AppStatus::SUCCESS;
}

AppStatus PipelineFactory::switch_pipeline(const pipeline_t &pipeline_type, bool start_pipeline)
{
    WEBSERVER_LOG_INFO("Pipeline switch requested to type: {}", static_cast<int>(pipeline_type));

    if (m_current_pipeline && pipeline_type == m_current_pipeline_type)
    {
        WEBSERVER_LOG_INFO("Requested pipeline type is already active: {}", static_cast<int>(pipeline_type));
        return AppStatus::SUCCESS;
    }

    auto prev_profile_type = ProfileType::Daylight; // default to Daylight unless we have a previous profile

    // Stop the current pipeline if it's running
    if (m_current_pipeline)
    {
        WEBSERVER_LOG_INFO("Stopping current pipeline before switch");
        prev_profile_type = m_current_pipeline->get_current_profile();
        WEBSERVER_LOG_DEBUG("Got previous profile type: {} and will startup new pipeline with it",
                            static_cast<int>(prev_profile_type));

        m_current_pipeline->uninitialize();
        m_current_pipeline->stop();
        m_media_library->unsubscribe_all_from_frontend();
        m_current_pipeline = nullptr;

        // Reset analytics DB (entries + configuration) so stale config IDs from
        // the previous pipeline don't cause the new pipeline's encoder to query
        // analytics data that will never arrive.
        AnalyticsDB::instance().reset_db();
    }

    m_current_pipeline_type = pipeline_type;
    m_current_pipeline = create_pipeline(pipeline_type);
    if (!m_current_pipeline)
    {
        WEBSERVER_LOG_ERROR("Failed to create new pipeline of type: {}", static_cast<int>(pipeline_type));
        return AppStatus::PIPELINE_ERROR;
    }

    m_current_pipeline->init(prev_profile_type);

    // Replace the current pipeline
    if (m_current_pipeline && start_pipeline)
    {
        WEBSERVER_LOG_INFO("Starting new pipeline after switch");
        m_current_pipeline->start();
    }

    WEBSERVER_LOG_INFO("Pipeline switch completed successfully");
    return AppStatus::SUCCESS;
}

void PipelineFactory::handle_detections_requirement(bool requires_detections)
{
    std::lock_guard<std::mutex> lock(m_pipeline_mutex);
    m_detections_required = requires_detections;
    const auto target = target_pipeline_for_detections(m_current_pipeline_type, requires_detections);
    if (!target)
    {
        return;
    }
    WEBSERVER_LOG_INFO("Detections requirement changed: requires_detections={}, switching {} -> {}",
                       requires_detections, static_cast<int>(m_current_pipeline_type), static_cast<int>(*target));
    if (switch_pipeline(*target) != AppStatus::SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Switch to pipeline {} for detections requirement failed", static_cast<int>(*target));
    }
}

void PipelineFactory::register_endpoints()
{
    m_resources.m_srv.Get("/ai_pipeline", std::function<nlohmann::json()>([this]() {
                              WEBSERVER_LOG_INFO("GET /ai_pipeline called");
                              nlohmann::json ai_pipeline_json;
                              ai_pipeline_json["active"] = public_pipeline_type(this->get_current_pipeline_type());
                              ai_pipeline_json["available"] = this->get_supported_pipeline_types();
                              WEBSERVER_LOG_INFO("GET /ai_pipeline completed");
                              return ai_pipeline_json;
                          }));

    m_resources.m_srv.Patch("/ai_pipeline", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PATCH /ai_pipeline called");
        if (!j_body.contains("active"))
        {
            WEBSERVER_LOG_ERROR("active not found in request body");
            throw std::runtime_error("Pipeline name not found in request body");
        }
        auto requested_pipeline = j_body["active"].get<pipeline_t>();
        std::vector<pipeline_t> supported_pipelines = this->get_supported_pipeline_types();
        if (std::find(supported_pipelines.begin(), supported_pipelines.end(), requested_pipeline) ==
            supported_pipelines.end())
        {
            WEBSERVER_LOG_ERROR("Pipeline name {} not found in available pipelines",
                                static_cast<int>(requested_pipeline));
            throw std::runtime_error("Pipeline name not found in available pipelines");
        }

        pipeline_t target;
        {
            std::lock_guard<std::mutex> lock(m_pipeline_mutex);
            target = effective_user_pipeline(requested_pipeline, m_detections_required);
        }
        WEBSERVER_LOG_INFO("PATCH /ai_pipeline: requested={}, target={}", static_cast<int>(requested_pipeline),
                           static_cast<int>(target));
        if (switch_pipeline(target) != AppStatus::SUCCESS)
        {
            WEBSERVER_LOG_ERROR("Failed to switch pipeline via event handler");
            throw std::runtime_error("Failed to switch pipeline");
        }

        // Return the updated ai_pipeline_json (mask the internal type for the client)
        nlohmann::json ai_pipeline_json;
        ai_pipeline_json["active"] = public_pipeline_type(this->get_current_pipeline_type());
        ai_pipeline_json["available"] = supported_pipelines;
        WEBSERVER_LOG_INFO("PATCH /ai_pipeline completed");
        return ai_pipeline_json;
    });
}
