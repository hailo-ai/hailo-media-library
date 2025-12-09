#include "media_library/media_library.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/utils.hpp"
#include "media_library/logger_macros.hpp"
#include "media_library/env_vars.hpp"
#include "media_library/common.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/sensor_registry.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <iterator>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>

#define MODULE_NAME LoggerType::Api

MediaLibrary::MediaLibrary()
{
    m_pipeline_state_change_callback = nullptr;
    m_profile_restricted_callback = nullptr;
    m_profile_restriction_done_callback = nullptr;
    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED;
    m_enable_profile_restriction = true;
    m_active_aaa_config_path = std::nullopt;

    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary instance created");
}

tl::expected<MediaLibraryPtr, media_library_return> MediaLibrary::create()
{
    static MediaLibConfigManagerCore medialib_config_manager_core;
    static std::atomic<size_t> idx = 0;
    auto media_lib = std::make_shared<MediaLibrary>();
    media_lib->m_media_lib_config_manager = std::make_unique<MediaLibConfigManager>(idx, medialib_config_manager_core);
    if (media_lib->m_media_lib_config_manager->initialize() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize media library config manager");
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    idx++;
    return media_lib;
}

media_library_return MediaLibrary::create_frontend(std::string frontend_config_string)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Creating frontend with config string");
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
    if (!frontend_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_expected.error();
    }
    m_frontend = frontend_expected.value();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend created successfully");

    auto result = m_frontend->set_config(frontend_config_string);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return result;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend configured successfully");

    result = update_frontend_config();
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update frontend config");
        return result;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend config updated successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::create_frontend(frontend_config_t frontend_config)
{
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
    if (!frontend_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_expected.error();
    }
    m_frontend = frontend_expected.value();

    auto config_return = m_frontend->set_config(frontend_config);
    if (config_return != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return config_return;
    }

    auto update_result = update_frontend_config();
    if (update_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update frontend config");
        return update_result;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend config updated successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::create_encoders(
    const std::map<output_stream_id_t, config_encoded_output_stream_t> &encoded_output_stream)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Creating encoders");
    for (const auto &entry : encoded_output_stream)
    {
        std::string stream_id = entry.first;

        LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating encoder for stream {}", stream_id);
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected =
            MediaLibraryEncoder::create(stream_id);
        if (!encoder_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoder for stream {}", stream_id);
            return encoder_expected.error();
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder created for stream {}", stream_id);

        m_encoders[stream_id] = encoder_expected.value();
        std::string encoder_config_string =
            std::visit([](auto &&config) -> std::string { return read_string_from_file(config.config_path); },
                       entry.second.encoding);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder config read successfully for stream {}", stream_id);
        ConfigManager config_manager_osd = ConfigManager(ConfigSchema::CONFIG_SCHEMA_OSD);
        ConfigManager config_manager_masking = ConfigManager(ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK);
        std::string osd_string = config_manager_osd.config_struct_to_string<config_stream_osd_t>(entry.second.osd);
        std::string masking_string =
            config_manager_masking.config_struct_to_string<privacy_mask_config_t>(entry.second.masking);

        // Parse individual JSON strings
        nlohmann::json encoding_json = nlohmann::json::parse(encoder_config_string);
        nlohmann::json osd_json = nlohmann::json::parse(osd_string);
        nlohmann::json masking_json = nlohmann::json::parse(masking_string);

        // Create unified JSON object with flat structure
        nlohmann::json unified_config;
        unified_config = encoding_json;
        unified_config["osd"] = osd_json["osd"];
        unified_config["privacy_mask"] = masking_json;

        // Convert back to string
        std::string unified_config_string = unified_config.dump();
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Config string for stream {}: {}", stream_id, unified_config_string);
        auto result = m_encoders[stream_id]->set_config(unified_config_string);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder for stream {}", stream_id);
            return result;
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder configured for stream {}", stream_id);
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "All encoders created and configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::initialize_thermal_throttling_monitor()
{
    if (!m_enable_profile_restriction)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    // Use the factory to create a ThrottlingStateMonitor instance
    m_throttling_monitor = ThrottlingStateMonitor::create();

    auto subscribe_result = m_throttling_monitor->subscribe(throttling_state_t::FULL_PERFORMANCE, [this]() {
        on_throttling_state_change(throttling_state_t::FULL_PERFORMANCE);
    });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to FULL_PERFORMANCE state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(throttling_state_t::FULL_PERFORMANCE_COOLING, [this]() {
        on_throttling_state_change(throttling_state_t::FULL_PERFORMANCE_COOLING);
    });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to FULL_PERFORMANCE_COOLING state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(throttling_state_t::THROTTLING_S0_HEATING, [this]() {
        on_throttling_state_change(throttling_state_t::THROTTLING_S0_HEATING);
    });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to THROTTLING_S0_HEATING state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(throttling_state_t::THROTTLING_S3_COOLING, [this]() {
        on_throttling_state_change(throttling_state_t::THROTTLING_S3_COOLING);
    });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to THROTTLING_S3_COOLING state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(throttling_state_t::THROTTLING_S4_HEATING, [this]() {
        on_throttling_state_change(throttling_state_t::THROTTLING_S4_HEATING);
    });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to THROTTLING_S4_HEATING state");
        return subscribe_result;
    }

    auto start_result = m_throttling_monitor->start();
    if (start_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start throttling monitor");
        return start_result;
    }

    auto state_change_result = on_throttling_state_change(m_throttling_monitor->get_active_state());
    if (state_change_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to handle initial throttling state change");
        return state_change_result;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Throttling monitor started successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::on_profile_restricted(
    std::function<void(config_profile_t, config_profile_t)> callback)
{
    m_profile_restricted_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::on_profile_restriction_done(std::function<void()> callback)
{
    m_profile_restriction_done_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::initialize(std::string medialib_config_string)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Initializing MediaLibrary with config string");

    m_media_lib_config_manager->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_NONE);

    auto config_result = m_media_lib_config_manager->configure_medialib(medialib_config_string);
    if (config_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure MediaLibrary with config string");
        return config_result;
    }

    auto frontend_result = create_frontend(m_media_lib_config_manager->get_frontend_config_as_string());
    if (frontend_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_result;
    }

    auto encoders_result = create_encoders(m_media_lib_config_manager->get_encoded_output_streams());
    if (encoders_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoders");
        return encoders_result;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary initialized successfully");

    auto override_result = set_override_parameters(m_media_lib_config_manager->get_current_profile());
    if (override_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set override parameters");
        return override_result;
    }

    auto isp_result = configure_isp_with_current_profile();
    if (isp_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure ISP");
        return isp_result;
    }

    auto &analytics_db = get_analytics_db();
    analytics_db.add_configuration(
        m_media_lib_config_manager->get_current_profile().application_settings.application_analytics);

    auto thermal_result = initialize_thermal_throttling_monitor();
    if (thermal_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start throttling monitor");
        return thermal_result;
    }

    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_STOPPED;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::update_frontend_config()
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Updating frontend config");
    tl::expected<frontend_config_t, media_library_return> updated_frontend_config = m_frontend->get_config();
    if (!updated_frontend_config.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get updated frontend config");
        return MEDIA_LIBRARY_ERROR;
    }
    m_media_lib_config_manager->set_frontend_config(updated_frontend_config.value());
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::initialize(
    std::string frontend_config_json_string,
    std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_stream)
{
    m_media_lib_config_manager->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_NONE);

    auto frontend_result = create_frontend(frontend_config_json_string);
    if (frontend_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_result;
    }

    auto encoders_result = create_encoders(encoded_output_stream);
    if (encoders_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoders");
        return encoders_result;
    }

    auto thermal_result = initialize_thermal_throttling_monitor();
    if (thermal_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start throttling monitor");
        return thermal_result;
    }
    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_STOPPED;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_privacy_mask(MediaLibraryEncoderPtr encoder,
                                                          const privacy_mask_config_t &privacy_mask_config)
{
    std::shared_ptr<PrivacyMaskBlender> privacy_mask_blender = encoder->get_privacy_mask_blender();
    if (privacy_mask_blender == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get privacy mask blender from encoder");
        return MEDIA_LIBRARY_ERROR;
    }
    auto ret = privacy_mask_blender->configure(std::make_unique<privacy_mask_config_t>(privacy_mask_config));
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure privacy mask blender");
        return ret;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_blenders(
    std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_streams)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Configuring blenders");
    for (const auto &entry : m_encoders)
    {
        std::string stream_id = entry.first;
        auto privacy_mask_result = configure_privacy_mask(entry.second, encoded_output_streams[stream_id].masking);
        if (privacy_mask_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure privacy mask for stream {}", stream_id);
            return privacy_mask_result;
        }
        // TODO add OSD configuration here
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "All blenders configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_frontend_encoder(
    frontend_config_t frontend_config,
    std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_streams)
{
    media_library_return frontend_config_return = m_frontend->set_config(frontend_config);
    if (frontend_config_return != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return frontend_config_return;
    }

    auto update_result = update_frontend_config();
    if (update_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update frontend config");
        return update_result;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend config updated successfully");
    for (const auto &entry : m_encoders)
    {
        auto encoder_config_result = entry.second->set_config(encoded_output_streams[entry.first].encoding);
        if (encoder_config_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder for stream {}", entry.first);
            return encoder_config_result;
        }
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "All encoders configured successfully");

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::restrict_profile_denoise_off()
{
    config_profile_t previous_profile = m_media_lib_config_manager->get_current_profile();
    if (m_media_lib_config_manager->get_restricted_profile_type() !=
        restricted_profile_type_t::RESTICTED_PROFILE_DENOISE)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Profile restriction update: Setting AI Denoise is restricted");
        if (previous_profile.iq_settings.denoise.enabled)
        {
            LOGGER__MODULE__WARNING(
                MODULE_NAME, "Current profile is restricted! (AI Denoise enabled) - Switching to default profile");
            config_profile_t restricted_profile = m_media_lib_config_manager->get_default_profile();
            if (restricted_profile.iq_settings.denoise.enabled)
            {
                LOGGER__MODULE__WARNING(MODULE_NAME, "Default profile has denoise enabled - disabling denoise");
                restricted_profile.iq_settings.denoise.enabled = false;
            }
            media_library_return result = set_override_parameters(restricted_profile);
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set restricted profile");
                return result;
            }
            LOGGER__MODULE__DEBUG(MODULE_NAME,
                                  "Restricted profile with denoise off set successfully - notifying user callback");
            if (m_profile_restricted_callback)
            {
                m_profile_restricted_callback(previous_profile, restricted_profile);
            }
        }
    }

    m_media_lib_config_manager->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_DENOISE);
    LOGGER__MODULE__WARNING(MODULE_NAME, "Profile restriction of AI denoise has been set");

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::on_throttling_state_change(throttling_state_t state)
{
    switch (state)
    {
    case throttling_state_t::FULL_PERFORMANCE: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to FULL_PERFORMANCE");
        if (m_media_lib_config_manager->get_restricted_profile_type() ==
            restricted_profile_type_t::RESTICTED_PROFILE_DENOISE)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Profile restriction update: Setting AI Denoise is allowed");
            m_media_lib_config_manager->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_NONE);
            if (m_profile_restriction_done_callback)
            {
                m_profile_restriction_done_callback();
            }
        }
        break;
    }
    case throttling_state_t::FULL_PERFORMANCE_COOLING: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to FULL_PERFORMANCE_COOLING");
        media_library_return result = restrict_profile_denoise_off();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to restrict profile denoise off during FULL_PERFORMANCE_COOLING state");
            return result;
        }
        break;
    }
    case throttling_state_t::THROTTLING_S0_HEATING: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to THROTTLING_S0_HEATING");
        media_library_return result = restrict_profile_denoise_off();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to restrict profile denoise off during THROTTLING_S0_HEATING state");
            return result;
        }
        break;
    }
    case throttling_state_t::THROTTLING_S3_COOLING: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to THROTTLING_S3_COOLING");
        if (m_media_lib_config_manager->get_restricted_profile_type() ==
            restricted_profile_type_t::RESTICTED_PROFILE_STREAMING)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Pipeline in normal thermal state - Enabling streaming");
            media_library_return result = start_pipeline_internal();
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start pipeline after THROTTLING_S3_COOLING state");
                return result;
            }
        }
        media_library_return result = restrict_profile_denoise_off();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to restrict profile denoise off during THROTTLING_S3_COOLING state");
            return result;
        }
        break;
    }
    case throttling_state_t::THROTTLING_S4_HEATING: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to THROTTLING_S4_HEATING");
        if (m_media_lib_config_manager->get_restricted_profile_type() !=
            restricted_profile_type_t::RESTICTED_PROFILE_STREAMING)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Pipeline in critical thermal state - Disabling streaming");
            media_library_return result = stop_pipeline_internal();
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop pipeline after THROTTLING_S4_HEATING state");
                return result;
            }
            m_media_lib_config_manager->set_restricted_profile_type(
                restricted_profile_type_t::RESTICTED_PROFILE_STREAMING);
        }
        break;
    }
    default:
        break;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_isp(bool reconfigure_required, config_profile_t &previous_profile,
                                                 config_profile_t &new_profile)
{
    bool automatic_algorithms_changed =
        previous_profile.iq_settings.automatic_algorithms_config != new_profile.iq_settings.automatic_algorithms_config;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Checking if pipeline 3aconfig and sensor entry reconfiguration is required: {}",
                          reconfigure_required);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Checking if 3A config changed: {}", automatic_algorithms_changed);
    if (reconfigure_required)
    {
        LOGGER__MODULE__INFO(MODULE_NAME,
                             "Configuring ISP files due to pipeline requiring restart or frontend required pause");
        return configure_isp_with_current_profile();
    }
    else if (automatic_algorithms_changed)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "3A config struct changed, updating 3A config file");
        std::string aaa_config_string;
        auto result = m_media_lib_config_manager->get_3a_config(aaa_config_string);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get 3A config from MediaLibConfigManager");
            return result;
        }
        result = update_3a_config_file(aaa_config_string);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update 3A config from API");
            return result;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_isp_with_current_profile()
{
    std::string aaa_config_content;
    auto status = m_media_lib_config_manager->get_3a_config(aaa_config_content);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get 3A config from MediaLibConfigManager");
        return status;
    }
    std::string sensor_entry_content;
    status = m_media_lib_config_manager->get_sensor_entry_config(sensor_entry_content);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor entry from MediaLibConfigManager");
        return status;
    }
    media_library_return result = configure_isp_files(aaa_config_content, sensor_entry_content);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure ISP with 3A config");
        return result;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

std::stringstream get_timestamped_stringstream()
{
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::stringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t_now), "%Y%m%d%H%M%S") << std::setw(3) << std::setfill('0')
              << ms.count();
    return timestamp;
}

media_library_return MediaLibrary::configure_isp_files(const std::string &_3aconfig, const std::string &sensor_entry)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Configuring ISP with new 3A config and sensor entry");
    LOGGER__MODULE__TRACE(MODULE_NAME, "Configuring ISP with 3A config: {} and sensor entry", _3aconfig);
    std::stringstream timestamp = get_timestamped_stringstream();

    // Construct destination file paths in /tmp/
    std::string new_3aconfig = "/tmp/TripleAConfig_" + timestamp.str() + ".json";
    size_t sensor_index = m_media_lib_config_manager->get_current_profile().sensor_config.input_video.sensor_index;
    std::string new_sensor_entry = "/tmp/Sensor" + std::to_string(sensor_index) + "Entry_" + timestamp.str() + ".json";
    // save file path
    m_active_aaa_config_path = new_3aconfig;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Dumping 3A config to {}", new_3aconfig);

    auto status = update_3a_config_file(_3aconfig);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update 3A config from API");
        return status;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Dumping sensor entry to {}", new_sensor_entry);
    // updating the sensor config file
    std::ofstream out_sensor_entry(new_sensor_entry);
    if (!out_sensor_entry.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file for writing: {}", new_sensor_entry);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    out_sensor_entry << sensor_entry;
    out_sensor_entry.close();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Sensor entry written to {}", new_sensor_entry);

    // Create or update symlinks
    std::string symlink_3aconfig = m_media_lib_config_manager->get_isp_3a_config_symlink_path();
    std::string symlink_sensor = m_media_lib_config_manager->get_isp_sensor_symlink_path();

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing old symlinks");
    try
    {
        safe_remove_symlink_target(symlink_3aconfig);
        safe_remove_symlink_target(symlink_sensor);
    }
    catch (fs::filesystem_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to remove old symlinks: {}", e.what());
    }

    try
    {
        fs::create_symlink(new_3aconfig, symlink_3aconfig);
        fs::create_symlink(new_sensor_entry, symlink_sensor);
    }
    catch (fs::filesystem_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create symlinks: {}", e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "ISP configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::update_3a_config_file(const std::string &_3aconfig_json)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Configuring ISP 3A config file");

    if (!m_active_aaa_config_path.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Active 3A config path is not set");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto isp_format_json = _3aconfig_json;
    std::ofstream out_3aconfig(m_active_aaa_config_path.value());
    if (!out_3aconfig.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file for writing: {}", m_active_aaa_config_path.value());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    out_3aconfig << isp_format_json;
    out_3aconfig.close();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "3A config written to {}", m_active_aaa_config_path.value());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::set_automatic_algorithm_configuration(std::string automatic_algorithms_json_string)
{
    if (automatic_algorithms_json_string.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Automatic algorithms json string is empty");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    automatic_algorithms_config_t automatic_algorithms_config;
    ConfigManager config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_AUTOMATIC_ALGORITHMS);
    media_library_return ret = config_manager.config_string_to_struct<automatic_algorithms_config_t>(
        automatic_algorithms_json_string, automatic_algorithms_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse automatic algorithms json string");
        return ret;
    }

    config_profile_t new_profile = m_media_lib_config_manager->get_current_profile();
    new_profile.iq_settings.automatic_algorithms_config = automatic_algorithms_config;
    return set_override_parameters(new_profile);
}

media_library_return MediaLibrary::set_override_parameters(config_profile_t profile)
{
    config_profile_t previous_profile = m_media_lib_config_manager->get_current_profile();

    // Verify that denoise / hdr / didn't change
    // Schema profile codec_configs entire encoder_config_t
    m_media_lib_config_manager->set_profile(profile);

    // Check if profile is valid in this thermal state
    if (!validate_profile_restrictions(m_media_lib_config_manager->get_current_profile()))
    {
        m_media_lib_config_manager->set_profile(previous_profile);
        return MEDIA_LIBRARY_PROFILE_IS_RESTRICTED;
    }
    config_profile_t new_profile = m_media_lib_config_manager->get_current_profile();
    bool restart_required = stream_restart_required(previous_profile, new_profile);
    bool frontend_pause_unpause_required = frontend_pause_required(previous_profile, new_profile, restart_required);
    if (restart_required)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Restarting pipeline");
        LOGGER__MODULE__INFO(MODULE_NAME, "stopping pipeline");
        media_library_return stop_result = stop_pipeline();
        if (stop_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop pipeline before profile change");
            return stop_result;
        }
    }
    else if (frontend_pause_unpause_required)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Pausing frontend pipeline");
        media_library_return pause_result = m_frontend->pause_pipeline();
        if (pause_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to pause frontend pipeline");
            return pause_result;
        }
    }

    bool reconfigure_isp_required = restart_required || frontend_pause_unpause_required;
    media_library_return result = configure_isp(reconfigure_isp_required, previous_profile, new_profile);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure ISP");
        return result;
    }

    media_library_return frontend_encoder_result = configure_frontend_encoder(
        m_media_lib_config_manager->get_frontend_config(), m_media_lib_config_manager->get_encoded_output_streams());
    if (frontend_encoder_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend and encoders after profile change");
        return frontend_encoder_result;
    }
    media_library_return blender_result = configure_blenders(m_media_lib_config_manager->get_encoded_output_streams());
    if (blender_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure blenders after profile change");
        return blender_result;
    }
    if (restart_required)
    {
        media_library_return start_result = start_pipeline();
        if (start_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start pipeline after profile change");
            return start_result;
        }
    }
    else if (frontend_pause_unpause_required)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Unpausing frontend pipeline");
        media_library_return unpause_result = m_frontend->unpause_pipeline();
        if (unpause_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to unpause frontend pipeline");
            return unpause_result;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_pipeline_state_t MediaLibrary::get_pipeline_state() const
{
    return m_pipeline_state;
}

media_library_return MediaLibrary::on_pipeline_state_change(
    std::function<void(media_library_pipeline_state_t)> callback)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting pipeline state change callback");
    m_pipeline_state_change_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibrary::validate_profile_restrictions(const config_profile_t &profile)
{
    if (!m_enable_profile_restriction)
    {
        return true;
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Validating profile restrictions");
    switch (m_media_lib_config_manager->get_restricted_profile_type())
    {
    case restricted_profile_type_t::RESTICTED_PROFILE_DENOISE: {
        if (profile.iq_settings.denoise.enabled)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME,
                                    "Validation of profile against restriction failed - requested AI Denoise enabled = "
                                    "true. this is a restricted profile on this thermal state");
            return false;
        }
        break;
    }
    case restricted_profile_type_t::RESTICTED_PROFILE_STREAMING: {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Pipeline in critical thermal state - streaming is disabled - cannot change profile");
        return false;
    }
    default:
        break;
    }
    return true;
}

bool MediaLibrary::stream_restart_required(config_profile_t previous_profile, config_profile_t new_profile)
{
    // ISP changes
    bool restart_required = false;

    restart_required |= previous_profile.iq_settings.hdr.enabled != new_profile.iq_settings.hdr.enabled;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Restart required due to hdr change: {}", restart_required);
    // Res changes
    for (const auto &resolution : previous_profile.application_settings.application_input_streams.resolutions)
    {
        if (std::find_if(new_profile.application_settings.application_input_streams.resolutions.begin(),
                         new_profile.application_settings.application_input_streams.resolutions.end(),
                         [&resolution](const auto &res) {
                             return resolution.dimensions_and_aspect_ratio_equal(res);
                         }) == new_profile.application_settings.application_input_streams.resolutions.end())
        {
            restart_required |= true;
            break;
        }
    }
    // if rotation is 90 or 180 restart is required
    restart_required |= previous_profile.application_settings.rotation.effective_value() !=
                        new_profile.application_settings.rotation.effective_value();
    return restart_required;
}

bool MediaLibrary::frontend_pause_required(config_profile_t previous_profile, config_profile_t new_profile,
                                           bool restart_required)
{
    // pause is not relevant when restart is required
    if (restart_required)
    {
        return false;
    }

    bool pause_required = false;
    // Check for sensor configuration changes
    auto &prev_sensor = previous_profile.sensor_config;
    auto &new_sensor = new_profile.sensor_config;
    auto &prev_res = prev_sensor.input_video.resolution;
    auto &new_res = new_sensor.input_video.resolution;

    pause_required |=
        prev_res.width != new_res.width || prev_res.height != new_res.height || prev_res.framerate != new_res.framerate;
    pause_required |= prev_sensor.input_video.source != new_sensor.input_video.source;
    pause_required |= prev_sensor.input_video.source_type != new_sensor.input_video.source_type;
    pause_required |= prev_sensor.input_video.sensor_index != new_sensor.input_video.sensor_index;
    pause_required |= prev_sensor.sensor_calibration_file_path != new_sensor.sensor_calibration_file_path;
    pause_required |= prev_sensor.sensor_configuration != new_sensor.sensor_configuration;
    pause_required |= prev_sensor.input_video.sensor_index != new_sensor.input_video.sensor_index;

    bool denoise_bayer_changed = new_profile.iq_settings.denoise.bayer != previous_profile.iq_settings.denoise.bayer;
    pause_required |= denoise_bayer_changed;
    return pause_required;
}

media_library_return MediaLibrary::set_profile(std::string profile_name)
{
    // verify that profile_name exists in medialib_config
    auto profiles = m_media_lib_config_manager->get_medialib_config().profiles;
    if (profiles.find(profile_name) == profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Profile name '{}' does not exist in medialib_config", profile_name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return status = set_override_parameters(profiles[profile_name]);
    return status;
}

tl::expected<config_profile_t, media_library_return> MediaLibrary::get_profile(const std::string &profile_name)
{
    auto profiles = m_media_lib_config_manager->get_medialib_config().profiles;
    auto it = profiles.find(profile_name);
    if (it != profiles.end())
    {
        return it->second;
    }

    return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
}

tl::expected<config_profile_t, media_library_return> MediaLibrary::get_current_profile()
{
    // TODO: sync encoder config sync frontend config
    return m_media_lib_config_manager->get_current_profile();
}

tl::expected<std::string, media_library_return> MediaLibrary::get_current_profile_str()
{
    config_profile_t current_profile = m_media_lib_config_manager->get_current_profile();
    std::string profile_string = m_media_lib_config_manager->profile_struct_to_string(current_profile);
    return profile_string;
}

media_library_return MediaLibrary::subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks)
{
    m_frontend->subscribe(fe_callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::subscribe_to_encoder_output(output_stream_id_t streamId, AppWrapperCallback callback)
{
    m_encoders[streamId]->subscribe(callback);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::start_pipeline()
{
    if (m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Pipeline is not initialized");
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_RUNNING)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Pipeline is already running");
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (m_media_lib_config_manager->get_restricted_profile_type() ==
        restricted_profile_type_t::RESTICTED_PROFILE_STREAMING)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Pipeline in critical thermal state - streaming is disabled");
        return MEDIA_LIBRARY_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline is stopped, proceeding to start it");
    return start_pipeline_internal();
}

media_library_return MediaLibrary::start_pipeline_internal()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Starting pipeline");
    for (const auto &entry : m_encoders)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Starting encoder for stream {}", entry.first);
        media_library_return result = entry.second->start();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start encoder for stream {}", entry.first);
            return result;
        }
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Starting frontend");
    media_library_return result = m_frontend->start();
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start frontend");
        return result;
    }

    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_RUNNING;
    if (m_pipeline_state_change_callback)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Invoking pipeline state change callback");
        m_pipeline_state_change_callback(m_pipeline_state);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline started successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::stop_pipeline()
{
    if (m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Pipeline is not initialized");
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_STOPPED)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Pipeline is already stopped");
        return MEDIA_LIBRARY_SUCCESS;
    }

    return stop_pipeline_internal();
}

media_library_return MediaLibrary::stop_pipeline_internal()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Stopping pipeline");
    media_library_return frontend_stop_result = m_frontend->stop();
    if (frontend_stop_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop frontend");
        return frontend_stop_result;
    }
    for (const auto &entry : m_encoders)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Stopping encoder for stream {}", entry.first);
        media_library_return encoder_stop_result = entry.second->stop();
        if (encoder_stop_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop encoder for stream {}", entry.first);
            return encoder_stop_result;
        }
    }

    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_STOPPED;
    if (m_pipeline_state_change_callback)
    {
        m_pipeline_state_change_callback(m_pipeline_state);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline stopped successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

AnalyticsDB &MediaLibrary::get_analytics_db()
{
    return AnalyticsDB::instance();
}

MediaLibrary::~MediaLibrary()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Destroying MediaLibrary instance");
    m_frontend = nullptr;
    m_encoders.clear();
    m_throttling_monitor->stop();
    std::string symlink_3aconfig = m_media_lib_config_manager->get_isp_3a_config_symlink_path();
    std::string symlink_sensor = m_media_lib_config_manager->get_isp_sensor_symlink_path();
    safe_remove_symlink_target(symlink_3aconfig);
    safe_remove_symlink_target(symlink_sensor);
}
