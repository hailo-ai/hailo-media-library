#include "pipeline.hpp"
#include "common/common.hpp"
#include <fstream>
#include <thread>

#define FRONTEND_STAGE "frontend_stage"
#define ENCODER_NAME(str) (std::string(str) + "_encoder")
#define UDP_NAME(str) (std::string(str) + "_udp")
#define HOST_IP "10.0.0.2"
#define HOST_PORT "5000"
#define TEE_STAGE "vision_tee"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline::overlay;
using namespace hailo_analytics::pipeline::routing;
using namespace webserver::pipeline;
using namespace webserver::resources;

#define EVENT_SUBSCRIBER_ID "base_pipeline_subscriber"

#define EVENT_SUBSCRIBER_ID "base_pipeline_subscriber"

BasePipeline::BasePipeline(WebserverResourceRepository resources, std::shared_ptr<MediaLibrary> media_library,
                           std::shared_ptr<RTPConverterStage> webrtc_stage, Architecture platform,
                           ProfileType default_profile, std::vector<ProfileType> supported_profiles)
    : m_resources(resources), m_app_resources(std::make_shared<AppResources>()), m_webrtc_stage(webrtc_stage),
      m_supported_profiles(supported_profiles), m_default_profile(default_profile)
{
    WEBSERVER_LOG_INFO("initializing Pipeline with platform: {}", ARCHITECTURE_STRING(platform));
    m_app_resources->platform = platform;
    m_app_resources->media_library = media_library;
    m_current_profile_type = default_profile;
    m_default_profile_type = default_profile;
    m_rotate_done_in_dewarp = is_env_variable_on(MEDIALIB_DEWARP_DSP_OPTIMIZATION_ENV_VAR);
    subscribe_callbacks();

    m_app_resources->m_isp_blender = std::make_shared<IspBlender>();
    m_app_resources->m_isp_blender->set_media_library(m_app_resources->media_library);
    m_resources->m_event_bus->notify(
        EventType::UPDATE_BLENDER,
        std::make_shared<ShareValueState<std::shared_ptr<IspBlender>>>(m_app_resources->m_isp_blender));

    WEBSERVER_LOG_INFO("initializing successfully");
}

BasePipeline::~BasePipeline()
{
    WEBSERVER_LOG_DEBUG("Destroying BasePipeline");
    m_resources->m_event_bus->unsubscribe_all(EVENT_SUBSCRIBER_ID);

    // Unsubscribe from media library callbacks to prevent callbacks on destroyed objects
    if (m_app_resources && m_app_resources->media_library)
    {
        m_app_resources->media_library->unsubscribe_from_profile_restriction_callbacks();
        m_app_resources->media_library->unsubscribe_from_throttling_state_change();
    }

    unregister_endpoints();
}

void BasePipeline::configure_profile_restriction_handlers()
{
    m_app_resources->media_library->set_restriction_fallback_profile(this->get_profile_name_by_type(
        ProfileType::Daylight)); // Set Daylight as the fallback profile when restriction occurs

    // Disable automatic profile switching on restriction
    m_app_resources->media_library->set_auto_profile_restriction_enabled(false);

    // Register on_profile_restricted callback
    m_app_resources->media_library->subscribe_to_profile_restricted(
        [this](const config_profile_t &previous_profile, const config_profile_t &new_profile) {
            on_pipeline_profile_restricted(previous_profile, new_profile);
        });

    // Register on_profile_restriction_done callback
    m_app_resources->media_library->subscribe_to_profile_restriction_done(
        [this]() { on_pipeline_profile_restriction_done(); });

    // Register throttling state change callback to emit throttling state updates
    m_app_resources->media_library->subscribe_to_throttling_state_change(
        [this](media_library_throttling_state_t throttling_state) {
            WEBSERVER_LOG_INFO("Throttling state changed to: {}", static_cast<int>(throttling_state));
            m_resources->m_event_bus->notify(EventType::THROTTLING_STATE_UPDATE,
                                             std::make_shared<MediaLibraryThrottlingState>(throttling_state));
        });

    // Emit initial throttling state
    auto initial_throttling_state_exp = m_app_resources->media_library->get_throttling_state();
    if (initial_throttling_state_exp.has_value())
    {
        media_library_throttling_state_t initial_throttling_state = initial_throttling_state_exp.value();
        WEBSERVER_LOG_INFO("Emitting initial throttling state: {}",
                           static_cast<nlohmann::json>(initial_throttling_state).dump());
        m_resources->m_event_bus->notify(EventType::THROTTLING_STATE_UPDATE,
                                         std::make_shared<MediaLibraryThrottlingState>(initial_throttling_state));
    }
    else
    {
        WEBSERVER_LOG_WARNING("Failed to get initial throttling state");
    }
}

void BasePipeline::init(ProfileType profile_type)
{
    // Register endpoints after derived class is fully constructed
    register_endpoints();

    // verify that all profiles are supported, remove any that are not
    std::vector<ProfileType> supported_profiles;
    for (const auto &profile_to_check : m_supported_profiles)
    {
        std::string profile_name = this->get_profile_name_by_type(profile_to_check);
        try
        {
            auto profile = m_app_resources->media_library->get_profile(profile_name);
            if (!profile.has_value())
            {
                WEBSERVER_LOG_WARNING("Profile {} not found in media library config, removing it", profile_name);
                continue;
            }
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_WARNING("Profile {} is not supported, removing it", profile_name);
            continue;
        }
        supported_profiles.push_back(profile_to_check);
        WEBSERVER_LOG_INFO("Profile {} is verified and supported", profile_name);
    }
    m_supported_profiles = supported_profiles;

    // verify profile type is in supported profiles and set it
    if (std::find(m_supported_profiles.begin(), m_supported_profiles.end(), profile_type) == m_supported_profiles.end())
    {
        WEBSERVER_LOG_WARN("profile type {} is not supported for pipeline {}, defaulting to {}",
                           static_cast<int>(profile_type), this->pipeline_name(),
                           static_cast<int>(m_default_profile_type));
        m_current_profile_type = m_default_profile_type;
    }
    else
    {
        m_current_profile_type = profile_type;
        WEBSERVER_LOG_INFO("Setting current profile to {}", static_cast<int>(m_current_profile_type));
    }

    std::string profile_to_set = this->get_profile_name_by_type(m_current_profile_type);
    media_library_return set_profile_status = m_app_resources->media_library->set_profile(profile_to_set);
    if (set_profile_status == MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
    {
        WEBSERVER_LOG_WARN("Profile {} is restricted due to thermal conditions", profile_to_set);
        auto fallback_profile = m_app_resources->media_library->get_current_profile();
        if (fallback_profile.has_value())
        {
            WEBSERVER_LOG_INFO("Fallback to profile {} due to thermal conditions", fallback_profile.value().name);
        }
        else
        {
            WEBSERVER_LOG_ERROR("Failed to get fallback profile (current profile) after restriction");
        }
    }
    else if (set_profile_status != MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile {} during pipeline initialization. error code {}", profile_to_set,
                            set_profile_status);
        throw std::runtime_error("Failed to set profile during pipeline initialization");
    }

    // Notify the event bus about profiles change
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    m_resources->m_event_bus->notify(EventType::PROFILE_UPDATE,
                                     std::make_shared<ProfileState>(ProfileStateData{
                                         current_profile, this->get_profile_type_by_name(current_profile.name),
                                         current_profile.name, this->get_supported_profiles()}));

    configure_profile_restriction_handlers();

    m_resources->m_event_bus->notify(EventType::ENCODER_CHANGE,
                                     std::make_shared<EncoderState>(EncoderState(DEFAULT_STREAM_4K_NAME)));
}

void BasePipeline::uninitialize()
{
    m_resources->m_event_bus->unsubscribe_all(EVENT_SUBSCRIBER_ID);

    // Unsubscribe from media library callbacks to prevent callbacks on destroyed objects
    if (m_app_resources && m_app_resources->media_library)
    {
        m_app_resources->media_library->unsubscribe_from_profile_restriction_callbacks();
        m_app_resources->media_library->unsubscribe_from_throttling_state_change();
    }

    unregister_endpoints();
}

void BasePipeline::on_pipeline_profile_restricted(config_profile_t previous_profile, config_profile_t new_profile)
{
    WEBSERVER_LOG_INFO("Profile restricted callback received - previous: {}, new: {}", previous_profile.name,
                       new_profile.name);
}

void BasePipeline::on_pipeline_profile_restriction_done()
{
    WEBSERVER_LOG_INFO("Profile restriction done callback received");
}

void BasePipeline::start()
{
    WEBSERVER_LOG_INFO("Starting Pipeline");
    std::cout << "Starting Pipeline..." << std::endl;
    // encoder changes all the time so the encoder resource get a pointer to pull the encoder config when its wants
    m_app_resources->media_library->start_pipeline();
    m_app_resources->pipeline->start();
    // Create pipeline
    sleep(1);
    auto encoder_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_ENCODER));
    encoder_resource->set_encoder_query([this]() { return this->get_encoder_config(); });

    m_resources->m_event_bus->notify(EventType::RESET_ISP, std::make_shared<EmptyState>(EmptyState()));

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    ProfileStateData current_profile_data{current_profile, this->get_profile_type_by_name(current_profile.name),
                                          current_profile.name, this->get_supported_profiles()};
    m_resources->m_event_bus->notify(EventType::PIPELINE_READY,
                                     std::make_shared<ProfileState>(ProfileState(current_profile_data)));

    WEBSERVER_LOG_INFO("BasePipeline started successfully");
}

void BasePipeline::stop()
{
    WEBSERVER_LOG_INFO("Stopping BasePipeline");
    std::cout << "Stopping Pipeline..." << std::endl;
    m_app_resources->pipeline->stop();
    m_app_resources->media_library->stop_pipeline();
    m_app_resources->media_library->shutdown();
    WEBSERVER_LOG_INFO("BasePipeline stopped successfully");
}

static bool is_hdr_profile(ProfileType type)
{
    return type == ProfileType::HighDynamicRange || type == ProfileType::DenoiseHdr;
}

void BasePipeline::subscribe_callbacks()
{
    WEBSERVER_LOG_INFO("Subscribing callbacks");

    using CallbackFunction = void (BasePipeline::*)(ResourceStateChangeNotification);
    std::map<EventType, CallbackFunction> event_callback_map = {
        {EventType::CHANGED_RESOURCE_OSD, &BasePipeline::callback_handle_osd},
        {EventType::CHANGED_RESOURCE_ENCODER, &BasePipeline::callback_handle_encoder},
        {EventType::CHANGED_RESOURCE_PRIVACY_MASK, &BasePipeline::callback_handle_privacy_mask},
        {EventType::SWITCH_PROFILE, &BasePipeline::callback_handle_profile_switch},
    };
    for (const auto &event_callback : event_callback_map)
    {
        m_resources->m_event_bus->subscribe(EVENT_SUBSCRIBER_ID, event_callback.first,
                                            EventPriority::EVENT_PRIORITY_MEDIUM,
                                            std::bind(event_callback.second, this, std::placeholders::_1));
    }
    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID,
        {EventType::CHANGE_FRAMERATE, EventType::CHANGE_RESOLUTION, EventType::CHANGE_FLIP, EventType::CHANGE_ROTATION,
         EventType::CHANGE_GRAYSCALE, EventType::CHANGE_DEWARP, EventType::CHANGE_FREEZE, EventType::CHANGE_VALVE,
         EventType::CHANGE_EIS, EventType::CHANGE_DIS, EventType::CHANGE_DIGITAL_ZOOM,
         EventType::CHANGE_DIGITAL_ZOOM_ROI},
        EventPriority::EVENT_PRIORITY_MEDIUM,
        std::bind(&BasePipeline::callback_handle_update_profile, this, std::placeholders::_1));

    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, {EventType::CHANGE_RESOLUTION, EventType::CHANGE_ROTATION},
        EventPriority::EVENT_PRIORITY_VERY_HIGH, [this](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("handeling before reset notification");
            m_resources->m_event_bus->notify(EventType::CHANGE_VALVE, std::make_shared<ProfileValveState>(false));
        });
    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, EventType::SWITCH_PROFILE, EventPriority::EVENT_PRIORITY_VERY_HIGH,
        [this](ResourceStateChangeNotification notification) {
            auto state = notification.getDirectResourceState<ProfileTypeState>();
            m_hdr_valve_active = (m_app_resources->platform == Architecture::Hailo15L) &&
                                 (is_hdr_profile(m_current_profile_type) || is_hdr_profile(state->value));
            if (!m_hdr_valve_active)
                return;
            WEBSERVER_LOG_INFO("handeling before reset notification");
            m_resources->m_event_bus->notify(EventType::CHANGE_VALVE, std::make_shared<ProfileValveState>(false));
        });
    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, {EventType::CHANGE_RESOLUTION, EventType::CHANGE_ROTATION},
        EventPriority::EVENT_PRIORITY_LOW, [this](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("handeling after reset notification");
            m_resources->m_event_bus->notify(EventType::RESET_ISP, std::make_shared<EmptyState>(EmptyState()));
            m_resources->m_event_bus->notify(EventType::CHANGE_VALVE, std::make_shared<ProfileValveState>(true));
        });
    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, EventType::SWITCH_PROFILE, EventPriority::EVENT_PRIORITY_LOW,
        [this](ResourceStateChangeNotification notification) {
            if (!m_hdr_valve_active)
                return;
            m_hdr_valve_active = false;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            WEBSERVER_LOG_INFO("handeling after reset notification");
            m_resources->m_event_bus->notify(EventType::RESET_ISP, std::make_shared<EmptyState>(EmptyState()));
            m_resources->m_event_bus->notify(EventType::CHANGE_VALVE, std::make_shared<ProfileValveState>(true));
        });
    m_resources->m_event_bus->subscribe(
        EVENT_SUBSCRIBER_ID, EventType::RESET_CONFIG, EventPriority::EVENT_PRIORITY_HIGH,
        [this](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("Resetting pipeline config to default");
            m_resources->m_event_bus->notify(EventType::SWITCH_PROFILE,
                                             std::make_shared<ProfileTypeState>(this->m_default_profile_type));

            auto res = m_app_resources->media_library->reset_profiles();
            if (res != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                WEBSERVER_LOG_ERROR("Failed to revert profiles overrides error: {}", res);
                throw std::runtime_error("Failed to revert profiles overrides");
            }

            auto current_profile = m_app_resources->media_library->get_current_profile().value();
            ProfileType current_profile_type = this->get_profile_type_by_name(current_profile.name);
            m_resources->m_event_bus->notify(
                EventType::PROFILE_UPDATE,
                std::make_shared<ProfileState>(ProfileStateData{current_profile, current_profile_type,
                                                                current_profile.name, m_supported_profiles}));
        });
}

std::shared_ptr<FrontendStage> BasePipeline::configure_frontend()
{
    WEBSERVER_LOG_INFO("Configuring frontend");
    m_app_resources->frontend = std::make_shared<FrontendStage>(FRONTEND_STAGE, 1, false, false);
    AppStatus frontend_config_status = m_app_resources->frontend->configure(m_app_resources->media_library->m_frontend);
    if (frontend_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << FRONTEND_STAGE << std::endl;
        throw std::runtime_error("Failed to configure frontend");
    }
    WEBSERVER_LOG_INFO("Frontend configured successfully");
    return m_app_resources->frontend;
}

std::shared_ptr<EncoderStage> BasePipeline::configure_encoder_and_osd(const std::string &stream_name)
{
    WEBSERVER_LOG_INFO("Creating encoder and osd for stream {}", stream_name);
    std::string enc_name = ENCODER_NAME(stream_name);
    m_app_resources->encoders[enc_name] = std::make_shared<EncoderStage>(enc_name, 3, true);
    AppStatus enc_config_status =
        m_app_resources->encoders[enc_name]->configure(m_app_resources->media_library->m_encoders[stream_name]);
    if (enc_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder " << enc_name << std::endl;
        throw std::runtime_error("Failed to configure encoder");
    }
    WEBSERVER_LOG_INFO("Creating encoder {}", enc_name);
    return m_app_resources->encoders[enc_name];
}

std::shared_ptr<UdpStage> BasePipeline::configure_udp(const std::string &stream_name)
{
    std::string udp_name = UDP_NAME(stream_name);
    WEBSERVER_LOG_INFO("Creating udp {}", udp_name);
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>(udp_name);
    AppStatus udp_config_status = udp_stage->configure(HOST_IP, HOST_PORT, EncodingType::H264);
    if (udp_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }
    WEBSERVER_LOG_INFO("Udp {} created successfully", udp_name);
    return udp_stage;
}

std::string BasePipeline::read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    WEBSERVER_LOG_INFO("Read config from file: {}", file_path);
    return file_string;
}
