#include "pipeline_factory.hpp"
#include "common/common.hpp"
#include "resources/common/events_utils.hpp"
#include <iostream>
#include <stdexcept>
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

#define EVENT_SUBSCRIBER_ID "pipeline_factory_subscriber"

PipelineFactory::PipelineFactory(WebserverResourceRepository resources, Architecture platform,
                                 const pipeline_t &initial_pipeline_type)
    : m_resources(std::move(resources)), m_platform(platform)
{
    WEBSERVER_LOG_INFO("Initializing PipelineFactory with initial type: {}", static_cast<int>(initial_pipeline_type));
    register_endpoints();
    m_current_pipeline_type = initial_pipeline_type;
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(m_resources->get(RESOURCE_CONFIG_MANAGER));
    std::string medialib_config_string = config->get_current_medialib_config().dump();
    tl::expected<std::shared_ptr<MediaLibrary>, media_library_return> media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return;
    }
    m_media_library = media_lib_expected.value();
    m_media_library->set_override_persistent_settings(true);
    if (m_media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }

    std::shared_ptr<webserver::resources::WebRtcResource> webrtc_resource =
        std::static_pointer_cast<WebRtcResource>(m_resources->get(RESOURCE_WEBRTC));
    m_webrtc_stage = RTPConverterStageBuild::create()
                         .set_stage_name("webrtc_stage")
                         .set_session_name("main")
                         .set_rtp_receiver(webrtc_resource)
                         .set_leaky_opt(true)
                         .set_trace_opt(false)
                         .buildptr();
    m_webrtc_stage->init();

    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, EventType::PROFILE_UPDATE_REQUEST, EventPriority::EVENT_PRIORITY_MEDIUM,
        [this](ResourceStateChangeNotification notification) {
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
            ProfileType profile_type = m_current_pipeline->get_profile_type_by_name(current_profile.name);
            m_resources->m_event_bus->notify(
                EventType::PROFILE_UPDATE,
                std::make_shared<ProfileState>(ProfileStateData{current_profile, profile_type, current_profile.name,
                                                                m_current_pipeline->get_supported_profiles()}));
        });

    m_resources->m_event_bus->subscribe(EVENT_SUBSCRIBER_ID, EventType::RESET_CONFIG,
                                        EventPriority::EVENT_PRIORITY_VERY_HIGH,
                                        [this, initial_pipeline_type](ResourceStateChangeNotification notification) {
                                            WEBSERVER_LOG_INFO("Received RESET_CONFIG notification");
                                            std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                                            if (switch_pipeline(initial_pipeline_type) != AppStatus::SUCCESS)
                                            {
                                                WEBSERVER_LOG_ERROR("Failed to reset current pipeline of type: {}",
                                                                    static_cast<int>(initial_pipeline_type));
                                                throw std::runtime_error("Failed to reset current pipeline");
                                            }
                                        });

    m_supported_pipelines = {pipeline_t::Basic, pipeline_t::Detection};
    if (ClipPipeline::is_supported(m_resources))
    {
        m_supported_pipelines.push_back(pipeline_t::CLIP);
    }
    if (FaceLandmarksPipeline::is_supported(m_resources))
    {
        m_supported_pipelines.push_back(pipeline_t::FaceLandmarks);
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
    // Stop the current pipeline if it exists
    if (m_current_pipeline)
    {
        m_current_pipeline->stop();
    }
    WEBSERVER_LOG_DEBUG("PipelineFactory destroyed");
}

std::shared_ptr<BasePipeline> PipelineFactory::get_current_pipeline()
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

    return m_current_pipeline;
}

pipeline_t PipelineFactory::get_current_pipeline_type()
{
    std::lock_guard<std::mutex> lock(m_pipeline_mutex);
    return m_current_pipeline_type;
}

std::shared_ptr<BasePipeline> PipelineFactory::create_pipeline(const pipeline_t &pipeline_type)
{
    WEBSERVER_LOG_INFO("Creating pipeline of type: {}", static_cast<int>(pipeline_type));

    if (pipeline_type == pipeline_t::Basic)
    {
        return std::make_shared<BasicPipeline>(m_resources, m_media_library, m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::Detection)
    {
        return std::make_shared<DetectionPipeline>(m_resources, m_media_library, m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::CLIP)
    {
        return std::make_shared<ClipPipeline>(m_resources, m_media_library, m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::ProfileManager)
    {
        return std::make_shared<ProfileManagerPipeline>(m_resources, m_media_library, m_webrtc_stage, m_platform);
    }
    else if (pipeline_type == pipeline_t::FaceLandmarks)
    {
        return std::make_shared<FaceLandmarksPipeline>(m_resources, m_media_library, m_webrtc_stage, m_platform);
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
        m_media_library->m_frontend->unsubscribe_all();
        m_current_pipeline = nullptr;
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

void PipelineFactory::register_endpoints()
{
    m_resources->m_srv->Get("/ai_pipeline", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /ai_pipeline called");
                                nlohmann::json ai_pipeline_json;
                                ai_pipeline_json["active"] = this->get_current_pipeline_type();
                                ai_pipeline_json["available"] = this->get_supported_pipeline_types();
                                WEBSERVER_LOG_INFO("GET /ai_pipeline completed");
                                return ai_pipeline_json;
                            }));

    m_resources->m_srv->Patch("/ai_pipeline", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PATCH /ai_pipeline called");
        if (!j_body.contains("active"))
        {
            WEBSERVER_LOG_ERROR("active not found in request body");
            throw std::runtime_error("Pipeline name not found in request body");
        }
        auto pipeline_name = j_body["active"].get<pipeline_t>();
        std::vector<pipeline_t> supported_pipelines = this->get_supported_pipeline_types();
        if (std::find(supported_pipelines.begin(), supported_pipelines.end(), pipeline_name) ==
            supported_pipelines.end())
        {
            WEBSERVER_LOG_ERROR("Pipeline name {} not found in available pipelines", static_cast<int>(pipeline_name));
            throw std::runtime_error("Pipeline name not found in available pipelines");
        }
        if (this->m_current_pipeline_type == pipeline_name)
        {
            WEBSERVER_LOG_INFO("Requested pipeline {} is already active", static_cast<int>(pipeline_name));
            nlohmann::json ai_pipeline_json;
            ai_pipeline_json["active"] = this->get_current_pipeline_type();
            ai_pipeline_json["available"] = supported_pipelines;
            WEBSERVER_LOG_INFO("PATCH /ai_pipeline completed");
            return ai_pipeline_json;
        }

        // The CLIP pipeline manages the shared MediaLibrary through its own CameraAppConstructor,
        // which calls stop_pipeline() + shutdown() independently. Switching directly to/from CLIP
        // leaves the MediaLibrary in a state that other pipelines can't properly restart from.
        // Transitioning through Basic first ensures the MediaLibrary is in a clean, consistent state.
        // (Nitzan HACK)
        if (pipeline_name == pipeline_t::CLIP && m_current_pipeline_type != pipeline_t::Basic)
        {
            WEBSERVER_LOG_INFO("Switching to Basic pipeline before switching to CLIP pipeline");
            if (switch_pipeline(pipeline_t::Basic) != AppStatus::SUCCESS)
            {
                WEBSERVER_LOG_ERROR("Failed to switch to Basic pipeline before switching to CLIP");
                throw std::runtime_error("Failed to switch pipeline");
            }
        }
        else if (m_current_pipeline_type == pipeline_t::CLIP && pipeline_name != pipeline_t::Basic)
        {
            WEBSERVER_LOG_INFO("Switching to Basic pipeline before switching away from CLIP pipeline");
            if (switch_pipeline(pipeline_t::Basic) != AppStatus::SUCCESS)
            {
                WEBSERVER_LOG_ERROR("Failed to switch to Basic pipeline before switching away from CLIP");
                throw std::runtime_error("Failed to switch pipeline");
            }
        }

        WEBSERVER_LOG_INFO("Switching to AI pipeline: {}", static_cast<int>(pipeline_name));
        if (switch_pipeline(pipeline_name) != AppStatus::SUCCESS)
        {
            WEBSERVER_LOG_ERROR("Failed to switch pipeline via event handler");
            throw std::runtime_error("Failed to switch pipeline");
        }

        // Return the updated ai_pipeline_json
        nlohmann::json ai_pipeline_json;
        ai_pipeline_json["active"] = this->get_current_pipeline_type();
        ai_pipeline_json["available"] = supported_pipelines;
        WEBSERVER_LOG_INFO("PUT /ai_pipeline completed");
        return ai_pipeline_json;
    });
}
