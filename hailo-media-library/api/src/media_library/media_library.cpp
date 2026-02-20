#include "media_library/media_library.hpp"
#include "analytics_db.hpp"
#include "config_manager.hpp"
#include "dma_memory_allocator.hpp"
#include "isp_manager.hpp"
#include "media_library/config_parser.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/utils.hpp"
#include "media_library/logger_macros.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/sensor_registry.hpp"
#include "media_library/isp_utils.hpp"
#include "files_utils.hpp"
#include "snapshot.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <tl/expected.hpp>
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
    m_throttling_state_change_callback = nullptr;
    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED;
    m_current_throttling_state = media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED;
    m_enable_profile_restriction = true;
    m_override_persistent_settings = false;
    m_restriction_fallback_profile = std::nullopt;
    m_default_backup_folder_path = "";

    // Order is important, dma mem allocator must be first
    DmaMemoryAllocator::get_instance();
    AnalyticsDB::instance();
    SensorRegistry::get_instance();
    ConfigManager::get_instance();
    SnapshotManager::get_instance();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary instance created");
}

tl::expected<MediaLibraryPtr, media_library_return> MediaLibrary::create()
{
    auto media_lib = std::make_shared<MediaLibrary>();
    return media_lib;
}

tl::expected<media_library_throttling_state_t, media_library_return> MediaLibrary::get_throttling_state() const
{
    return m_current_throttling_state;
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

    m_frontend->set_config_manager_interactor(*m_config_manager_interactor.get());
    auto result = m_frontend->set_config(frontend_config_string);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return result;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend configured successfully");
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

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::create_encoder(output_stream_id_t stream_id,
                                                  config_encoded_output_stream_t encoded_output_stream)
{
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

    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No profile is currently in use");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto current_profile = current_profile_opt.value();
    size_t sensor_index = static_cast<size_t>(current_profile->sensor_config.input_video.sensor_id);
    m_encoders[stream_id]->set_sensor_index(sensor_index);

    std::string encoder_config_string =
        std::visit([](auto &&config) -> std::string { return read_string_from_file(config.config_path); },
                   encoded_output_stream.encoding);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder config read successfully for stream {}", stream_id);
    ConfigParser config_manager_osd = ConfigParser(ConfigSchema::CONFIG_SCHEMA_OSD);
    ConfigParser config_manager_masking = ConfigParser(ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK);
    std::string osd_string = config_manager_osd.config_struct_to_string<config_stream_osd_t>(encoded_output_stream.osd);
    std::string masking_string =
        config_manager_masking.config_struct_to_string<privacy_mask_config_t>(encoded_output_stream.masking);

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
    m_encoders[stream_id]->set_config_manager_interactor(*m_config_manager_interactor.get());
    std::string unified_config_string = unified_config.dump();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Config string for stream {}: {}", stream_id, unified_config_string);
    auto result = m_encoders[stream_id]->set_config(unified_config_string);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder for stream {}", stream_id);
        return result;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder configured for stream {}", stream_id);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::create_encoders(
    const std::map<output_stream_id_t, config_encoded_output_stream_t> &encoded_output_stream)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Creating encoders");
    for (const auto &entry : encoded_output_stream)
    {
        if (m_encoders.find(entry.first) != m_encoders.end())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder for stream {} already exists, skipping creation", entry.first);
            continue;
        }

        auto result = create_encoder(entry.first, entry.second);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoder for stream {}", entry.first);
            return result;
        }
    }

    // remove any encoders that are not in the new configuration
    std::vector<output_stream_id_t> encoders_to_remove;
    for (const auto &existing_encoder : m_encoders)
    {
        if (encoded_output_stream.find(existing_encoder.first) == encoded_output_stream.end())
        {
            encoders_to_remove.push_back(existing_encoder.first);
        }
    }
    for (const auto &encoder_id : encoders_to_remove)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing encoder for stream {}", encoder_id);
        m_encoders.erase(encoder_id);
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "All encoders created and configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_throttling_state_t MediaLibrary::convert_to_media_library_throttling_state(throttling_state_t state)
{
    switch (state)
    {
    case throttling_state_t::THERMAL_UNINITIALIZED:
        return media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED;
    case throttling_state_t::FULL_PERFORMANCE:
        return media_library_throttling_state_t::THROTTLING_STATE_FULL_PERFORMANCE;
    case throttling_state_t::FULL_PERFORMANCE_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_COOLING;
    case throttling_state_t::THROTTLING_S0_HEATING:
    case throttling_state_t::THROTTLING_S0_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_S0;
    case throttling_state_t::THROTTLING_S1_HEATING:
    case throttling_state_t::THROTTLING_S1_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_S1;
    case throttling_state_t::THROTTLING_S2_HEATING:
    case throttling_state_t::THROTTLING_S2_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_S2;
    case throttling_state_t::THROTTLING_S3_HEATING:
    case throttling_state_t::THROTTLING_S3_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_S3;
    case throttling_state_t::THROTTLING_S4_HEATING:
    case throttling_state_t::THROTTLING_S4_COOLING:
        return media_library_throttling_state_t::THROTTLING_STATE_S4;
    default:
        return media_library_throttling_state_t::THROTTLING_STATE_UNINITIALIZED;
    }
}

media_library_return MediaLibrary::initialize_thermal_throttling_monitor()
{
    // Use the factory to create a ThrottlingStateMonitor instance
    m_throttling_monitor = ThrottlingStateMonitor::create();

    // Start monitoring first and get user ID
    auto start_result = m_throttling_monitor->start();
    if (!start_result.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start throttling monitor");
        return start_result.error();
    }

    m_throttling_monitor_user_id = start_result.value();
    LOGGER__MODULE__INFO(MODULE_NAME, "Throttling monitor started with user_id: {}", m_throttling_monitor_user_id);

    // Now subscribe with the user ID
    auto subscribe_result =
        m_throttling_monitor->subscribe(m_throttling_monitor_user_id, throttling_state_t::FULL_PERFORMANCE,
                                        [this]() { on_throttling_state_change(throttling_state_t::FULL_PERFORMANCE); });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to FULL_PERFORMANCE state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(
        m_throttling_monitor_user_id, throttling_state_t::FULL_PERFORMANCE_COOLING,
        [this]() { on_throttling_state_change(throttling_state_t::FULL_PERFORMANCE_COOLING); });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to FULL_PERFORMANCE_COOLING state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(
        m_throttling_monitor_user_id, throttling_state_t::THROTTLING_S0_HEATING,
        [this]() { on_throttling_state_change(throttling_state_t::THROTTLING_S0_HEATING); });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to THROTTLING_S0_HEATING state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(
        m_throttling_monitor_user_id, throttling_state_t::THROTTLING_S3_COOLING,
        [this]() { on_throttling_state_change(throttling_state_t::THROTTLING_S3_COOLING); });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to THROTTLING_S3_COOLING state");
        return subscribe_result;
    }

    subscribe_result = m_throttling_monitor->subscribe(
        m_throttling_monitor_user_id, throttling_state_t::THROTTLING_S4_HEATING,
        [this]() { on_throttling_state_change(throttling_state_t::THROTTLING_S4_HEATING); });
    if (subscribe_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to subscribe to THROTTLING_S4_HEATING state");
        return subscribe_result;
    }

    auto state_change_result = on_throttling_state_change(m_throttling_monitor->get_active_state());
    if (state_change_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to handle initial throttling state change");
        return state_change_result;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Throttling monitor initialized successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::subscribe_to_profile_restricted(
    std::function<void(const config_profile_t &, const config_profile_t &)> callback)
{
    m_profile_restricted_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::on_profile_restricted(
    std::function<void(config_profile_t, config_profile_t)> callback)
{
    LOG_DEPRECATED_ONCE(on_profile_restricted, "MediaLibrary::on_profile_restricted is deprecated, please use "
                                               "MediaLibrary::subscribe_to_profile_restricted instead.");
    return subscribe_to_profile_restricted(
        [callback](const config_profile_t &prev_prfofile, const config_profile_t &new_profile) {
            callback(prev_prfofile, new_profile);
        });
}

media_library_return MediaLibrary::on_profile_restriction_done(std::function<void()> callback)
{
    LOG_DEPRECATED_ONCE(on_profile_restriction_done,
                        "MediaLibrary::on_profile_restriction_done is deprecated, please use "
                        "MediaLibrary::subscribe_to_profile_restriction_done instead.");
    return subscribe_to_profile_restriction_done(callback);
}

media_library_return MediaLibrary::subscribe_to_profile_restriction_done(std::function<void()> callback)
{
    m_profile_restriction_done_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::set_auto_profile_restriction_enabled(bool enabled)
{
    m_enable_profile_restriction = enabled;
    LOGGER__MODULE__INFO(MODULE_NAME, "Profile restriction (automatic profile switch on restriction) is {}",
                         enabled ? "enabled" : "disabled");
    return MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibrary::get_auto_profile_restriction_enabled()
{
    return m_enable_profile_restriction;
}

media_library_return MediaLibrary::initialize(std::string medialib_config_string, bool should_restore_backup)
{
    // Extract backup folder path from config if not already set
    if (m_default_backup_folder_path.empty())
    {
        nlohmann::json config_json = nlohmann::json::parse(medialib_config_string, nullptr, false);
        if (!config_json.is_discarded() && config_json.contains("backup_folder_path"))
        {
            m_default_backup_folder_path = config_json["backup_folder_path"].get<std::string>();
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Extracted backup folder path from config: {}",
                                  m_default_backup_folder_path);
        }
    }

    if (m_default_backup_folder_path.empty() || !should_restore_backup)
    {
        // Fall back to original config
        return initialize_internal(medialib_config_string);
    }

    std::string backup_medialib_path = m_default_backup_folder_path + "/medialib_config.json";
    if (!std::filesystem::exists(backup_medialib_path))
    {
        // Fall back to original config
        return initialize_internal(medialib_config_string);
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Found backup config at: {}", backup_medialib_path);
    auto backup_string_opt = files_utils::read_string_from_file(backup_medialib_path);
    if (!backup_string_opt.has_value() || backup_string_opt.value().empty())
    {
        // Fall back to original config
        return initialize_internal(medialib_config_string);
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Attempting to initialize from backup config");
    media_library_return result = initialize_internal(backup_string_opt.value());
    if (result == MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Successfully initialized from backup config");
        return MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__WARNING(MODULE_NAME, "Failed to initialize from backup config, falling back to original config");
    return initialize_internal(medialib_config_string);
}

media_library_return MediaLibrary::initialize_internal(std::string medialib_config_string)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Initializing MediaLibrary with config string");

    auto config_manager_interactor_res = ConfigManagerInteractor::create(medialib_config_string);
    if (!config_manager_interactor_res.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create config manager interactor");
        return config_manager_interactor_res.error();
    }

    m_config_manager_interactor = std::move(config_manager_interactor_res.value());

    auto frontend_result = create_frontend(m_config_manager_interactor->get_frontend_config_as_string());
    if (frontend_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_result;
    }

    m_config_manager_interactor->update_encoder_streams_for_rotation();

    auto encoders_result = create_encoders(m_config_manager_interactor->get_encoded_output_streams());
    if (encoders_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoders");
        return encoders_result;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary initialized successfully");

    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile is set in the configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    auto override_result = set_override_parameters(*current_profile_opt.value());
    if (override_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set override parameters");
        return override_result;
    }

    auto &analytics_db = get_analytics_db();
    analytics_db.add_configuration(current_profile_opt.value()->application_settings.application_analytics);

    auto thermal_result = initialize_thermal_throttling_monitor();
    if (thermal_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start throttling monitor");
        return thermal_result;
    }

    m_pipeline_state = media_library_pipeline_state_t::PIPELINE_STATE_STOPPED;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::initialize(
    std::string frontend_config_json_string,
    std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_stream)
{
    m_config_manager_interactor->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_NONE);

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

media_library_return MediaLibrary::configure_osd(MediaLibraryEncoderPtr encoder, const config_stream_osd_t &osd_config)
{
    ConfigParser config_manager_osd = ConfigParser(ConfigSchema::CONFIG_SCHEMA_OSD);
    std::string osd_string = config_manager_osd.config_struct_to_string<config_stream_osd_t>(osd_config);
    std::shared_ptr<osd::Blender> osd_blender = encoder->get_osd_blender();
    if (osd_blender == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get OSD blender from encoder");
        return MEDIA_LIBRARY_ERROR;
    }
    auto ret = osd_blender->configure(osd_string);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure OSD blender");
        return ret;
    }
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

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend config updated successfully");
    for (const auto &entry : m_encoders)
    {
        auto encoder_config_result = entry.second->set_config(encoded_output_streams[entry.first].encoding);
        if (encoder_config_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder for stream {}", entry.first);
            return encoder_config_result;
        }

        size_t sensor_index = frontend_config.input_config.sensor_index;
        entry.second->set_sensor_index(sensor_index);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "All encoders configured successfully");

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::restriction_auto_switch_to_fallback_profile(config_profile_t &fallback_profile)
{
    if (fallback_profile.iq_settings.denoise.enabled)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Current profile is restricted, tried to switch to fallback profile {} but "
                              "this profile contains Denoise enabled - please verify the profile you use "
                              "as default or change to fallback profile",
                              fallback_profile.name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Switching to fallback profile '{}' due to restriction", fallback_profile.name);
    media_library_return result = set_override_parameters(fallback_profile);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        if (m_restriction_fallback_profile.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to set restricted profile - please make sure the fallback profile is valid ("
                                  "{})",
                                  m_restriction_fallback_profile.value());
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to set restricted profile - please make sure the default profile is "
                                  "valid and aligned to your current profile, or you can set a different "
                                  "throttling fallback profile using API set_restriction_fallback_profile");
        }
        return result;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Restricted profile with denoise off set successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::restrict_denoise_off_switch_to_fallback_profile(
    const config_profile_t &current_profile)
{
    config_profile_t fallback_profile;
    if (m_restriction_fallback_profile.has_value())
    {
        LOGGER__MODULE__WARNING(MODULE_NAME,
                                "Current profile is restricted! (AI Denoise enabled), fallback profile is {}",
                                m_restriction_fallback_profile.value());
        auto fallback_profile_opt =
            m_config_manager_interactor->get_profile_by_name(m_restriction_fallback_profile.value());
        if (!fallback_profile_opt.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Fallback restriction profile '{}' not found",
                                  m_restriction_fallback_profile.value());
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        fallback_profile = *fallback_profile_opt.value();
    }
    else
    {
        fallback_profile = *m_config_manager_interactor->get_default_profile();
        LOGGER__MODULE__WARNING(MODULE_NAME,
                                "Current profile is restricted! (AI Denoise enabled), fallback profile is default ({})",
                                fallback_profile.name);
    }

    if (restriction_auto_switch_to_fallback_profile(fallback_profile) != MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (m_profile_restricted_callback)
    {
        m_profile_restricted_callback(current_profile, fallback_profile);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::restrict_profile_denoise_off()
{
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile is set in the configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    std::shared_ptr<const config_profile_t> current_profile = current_profile_opt.value();
    if (m_config_manager_interactor->get_restricted_profile_type() ==
        restricted_profile_type_t::RESTICTED_PROFILE_DENOISE)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Profile {} is already restricted with denoise off, no action needed",
                              current_profile->name);
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (!current_profile->iq_settings.denoise.enabled)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Current profile denoise is disabled, no action needed");
        m_config_manager_interactor->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_DENOISE);
        return MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    if (m_enable_profile_restriction)
    {
        ret = restrict_denoise_off_switch_to_fallback_profile(*current_profile);
        LOGGER__MODULE__WARNING(MODULE_NAME, "Profile restriction of AI denoise has been set");
    }

    m_config_manager_interactor->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_DENOISE);

    return ret;
}

media_library_return MediaLibrary::handle_restricted_streaming_state()
{
    if (m_config_manager_interactor->get_restricted_profile_type() ==
        restricted_profile_type_t::RESTICTED_PROFILE_STREAMING)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Pipeline in normal thermal state - Enabling streaming");
        media_library_return result = start_pipeline_internal();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to start pipeline after getting out of restricted streaming state");
            return result;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::on_throttling_state_change(throttling_state_t state)
{
    // Single lock for entire method - recursive_mutex allows user callbacks to call set_profile() safely
    std::lock_guard<std::recursive_mutex> lock(m_profile_change_mutex);

    media_library_throttling_state_t previous_state = m_current_throttling_state;
    m_current_throttling_state = convert_to_media_library_throttling_state(state);
    if (previous_state != m_current_throttling_state && m_throttling_state_change_callback != nullptr)
    {
        m_throttling_state_change_callback(m_current_throttling_state);
    }

    switch (state)
    {
    case throttling_state_t::FULL_PERFORMANCE: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to FULL_PERFORMANCE");
        media_library_return result = handle_restricted_streaming_state();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to handle restricted streaming state during FULL_PERFORMANCE state");
            return result;
        }
        if (m_config_manager_interactor->get_restricted_profile_type() ==
            restricted_profile_type_t::RESTICTED_PROFILE_DENOISE)
        {
            m_config_manager_interactor->set_restricted_profile_type(restricted_profile_type_t::RESTICTED_PROFILE_NONE);
            if (m_enable_profile_restriction)
            {
                LOGGER__MODULE__WARNING(MODULE_NAME, "Profile restriction update: Setting AI Denoise is allowed");
                if (m_profile_restriction_done_callback)
                {
                    m_profile_restriction_done_callback();
                }
            }
        }
        break;
    }
    case throttling_state_t::FULL_PERFORMANCE_COOLING: {
        LOGGER__MODULE__INFO(MODULE_NAME, "Handling thermal state change to FULL_PERFORMANCE_COOLING");
        media_library_return result = handle_restricted_streaming_state();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to handle restricted streaming state during FULL_PERFORMANCE_COOLING state");
            return result;
        }
        result = restrict_profile_denoise_off();
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
        media_library_return result = handle_restricted_streaming_state();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to handle restricted streaming state during THROTTLING_S0_HEATING state");
            return result;
        }
        result = restrict_profile_denoise_off();
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
        media_library_return result = handle_restricted_streaming_state();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Failed to handle restricted streaming state during THROTTLING_S3_COOLING state");
            return result;
        }
        result = restrict_profile_denoise_off();
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
        if (m_config_manager_interactor->get_restricted_profile_type() !=
            restricted_profile_type_t::RESTICTED_PROFILE_STREAMING)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Pipeline in critical thermal state - Disabling streaming");
            media_library_return result = stop_pipeline_internal();
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop pipeline after THROTTLING_S4_HEATING state");
                return result;
            }
            m_config_manager_interactor->set_restricted_profile_type(
                restricted_profile_type_t::RESTICTED_PROFILE_STREAMING);
        }
        break;
    }
    default:
        break;
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

media_library_return MediaLibrary::set_automatic_algorithm_configuration(std::string automatic_algorithms_json_string)
{
    std::lock_guard<std::recursive_mutex> lock(m_profile_change_mutex);
    if (automatic_algorithms_json_string.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Automatic algorithms json string is empty");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    automatic_algorithms_config_t automatic_algorithms_config;
    ConfigParser config_manager = ConfigParser(ConfigSchema::CONFIG_SCHEMA_AUTOMATIC_ALGORITHMS);
    media_library_return ret = config_manager.config_string_to_struct<automatic_algorithms_config_t>(
        automatic_algorithms_json_string, automatic_algorithms_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse automatic algorithms json string");
        return ret;
    }

    auto new_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!new_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile is set in the configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    config_profile_t new_profile = *new_profile_opt.value();
    new_profile.iq_settings.automatic_algorithms_config = automatic_algorithms_config;
    return set_override_parameters(new_profile);
}

media_library_return MediaLibrary::set_override_parameters(const config_profile_t &profile)
{
    std::lock_guard<std::recursive_mutex> lock(m_profile_change_mutex);

    auto previous_profile_opt = m_config_manager_interactor->get_current_profile();
    auto &previous_profile = previous_profile_opt.value();

    // Check if profile is valid in this thermal state
    auto &new_profile = profile;

    if (!validate_profile_thermal_restrictions(new_profile))
    {
        m_config_manager_interactor->set_profile(*previous_profile);
        return MEDIA_LIBRARY_PROFILE_IS_RESTRICTED;
    }
    bool restart_required = stream_restart_required(*previous_profile, new_profile);
    bool frontend_pause_unpause_required = frontend_pause_required(*previous_profile, new_profile, restart_required);
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
        for (auto &encoder : m_encoders)
        {
            encoder.second->force_keyframe();
        }
    }

    // Verify that denoise / hdr / didn't change
    // Schema profile codec_configs entire encoder_config_t
    m_config_manager_interactor->set_profile(profile);

    // Update encoder stream dimensions based on rotation config
    m_config_manager_interactor->update_encoder_streams_for_rotation();

    if (validate_profile_rules() != MEDIA_LIBRARY_SUCCESS)
    {
        m_config_manager_interactor->set_profile(*previous_profile);
        if (restart_required)
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "Reverting pipeline start after profile validation failure");
            media_library_return start_result = start_pipeline();
            if (start_result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start pipeline after reverting profile change");
                return start_result;
            }
        }
        else if (frontend_pause_unpause_required)
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "Unpausing frontend pipeline after profile validation failure");
            media_library_return unpause_result = m_frontend->unpause_pipeline();
            if (unpause_result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME,
                                      "Failed to unpause frontend pipeline after reverting profile change");
                return unpause_result;
            }
        }
        LOGGER__MODULE__ERROR(MODULE_NAME, "New profile validation against rules failed");
        return MEDIA_LIBRARY_PROFILE_VALIDATION_FAILED;
    }

    // if encoders is different than what is set in profile, recreate encoders
    if (new_profile.to_encoder_config_map() != previous_profile->to_encoder_config_map())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Recreating encoders due to profile change");
        media_library_return encoder_result =
            create_encoders(m_config_manager_interactor->get_encoded_output_streams());
        if (encoder_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to recreate encoders after profile change");
            return encoder_result;
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "Recreated {} encoders successfully",
                             m_config_manager_interactor->get_encoded_output_streams().size());
    }

    // set all configurations
    media_library_return frontend_encoder_result = configure_frontend_encoder(
        m_config_manager_interactor->get_frontend_config(), m_config_manager_interactor->get_encoded_output_streams());
    if (frontend_encoder_result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend and encoders after profile change");
        return frontend_encoder_result;
    }
    if (restart_required)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Starting pipeline after profile change");
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

    bool automatic_algorithms_changed = new_profile.iq_settings.automatic_algorithms_config !=
                                        previous_profile->iq_settings.automatic_algorithms_config;
    bool is_pipeline_running = m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_RUNNING;
    if (automatic_algorithms_changed && !restart_required && !frontend_pause_unpause_required && is_pipeline_running)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "3A config struct changed, updating 3A config file");
        media_library_return result = update_3a_config_file();
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update 3A config from API");
            return result;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::update_3a_config_file()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Configuring ISP 3A config file");

    auto config_path_opt = m_config_manager_interactor->get_isp_3a_config_symlink_path();
    if (!config_path_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get ISP 3A config path");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    std::string config_path = config_path_opt.value();

    if (!std::filesystem::exists(config_path))
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "3A config file not found: {}, nothing to update", config_path);
        return MEDIA_LIBRARY_SUCCESS;
    }

    std::string _3aconfig_json = m_config_manager_interactor->get_3a_config();

    auto isp_format_json = _3aconfig_json;
    std::ofstream out_3aconfig(config_path);
    if (!out_3aconfig.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file for writing: {}", config_path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    out_3aconfig << isp_format_json;
    out_3aconfig.close();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "3A config written to {}", config_path);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_pipeline_state_t MediaLibrary::get_pipeline_state() const
{
    return m_pipeline_state;
}

media_library_return MediaLibrary::subscribe_to_pipeline_state_change(
    std::function<void(media_library_pipeline_state_t)> callback)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting pipeline state change callback");
    m_pipeline_state_change_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::on_pipeline_state_change(
    std::function<void(media_library_pipeline_state_t)> callback)
{
    LOG_DEPRECATED_ONCE(on_pipeline_state_change, "MediaLibrary::on_pipeline_state_change is deprecated, please use "
                                                  "MediaLibrary::subscribe_to_pipeline_state_change instead.");
    return subscribe_to_pipeline_state_change(callback);
}

media_library_return MediaLibrary::subscribe_to_throttling_state_change(
    std::function<void(media_library_throttling_state_t)> callback)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting throttling state change callback");
    m_throttling_state_change_callback = callback;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::unsubscribe_from_profile_restriction_callbacks()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing profile restriction callbacks");
    m_profile_restricted_callback = nullptr;
    m_profile_restriction_done_callback = nullptr;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::unsubscribe_from_throttling_state_change()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing throttling state change callback");
    m_throttling_state_change_callback = nullptr;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::validate_profile_rules()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Validating profile against rules");
    auto expected_profile_json_str = get_current_profile_str();
    if (!expected_profile_json_str.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile as string for validation");
        return expected_profile_json_str.error();
    }
    return m_config_manager_interactor->validate_profile_rules(expected_profile_json_str.value());
}

bool MediaLibrary::validate_profile_thermal_restrictions(const config_profile_t &profile)
{
    if (!m_enable_profile_restriction)
    {
        return true;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Validating profile restrictions");
    switch (m_config_manager_interactor->get_restricted_profile_type())
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

EncoderType MediaLibrary::get_encoder_type(const encoder_config_t &config_variant)
{
    return std::visit(
        [](const auto &config) -> EncoderType {
            using T = std::decay_t<decltype(config)>;
            if constexpr (std::is_same_v<T, hailo_encoder_config_t>)
            {
                return EncoderType::Hailo;
            }
            else if constexpr (std::is_same_v<T, jpeg_encoder_config_t>)
            {
                return EncoderType::Jpeg;
            }
            return EncoderType::Hailo;
        },
        config_variant);
}

bool MediaLibrary::stream_restart_required(config_profile_t previous_profile, config_profile_t new_profile)
{
    // ISP changes
    bool restart_required = false;

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

    // Check if any encoder type changed (H.26x ↔ JPEG)
    auto prev_encoder_map = previous_profile.to_encoder_config_map();
    auto new_encoder_map = new_profile.to_encoder_config_map();

    for (const auto &entry : new_encoder_map)
    {
        const auto &stream_id = entry.first;

        // Check if stream exists in both profiles
        if (prev_encoder_map.find(stream_id) != prev_encoder_map.end())
        {
            EncoderType prev_type = get_encoder_type(prev_encoder_map[stream_id]);
            EncoderType new_type = get_encoder_type(new_encoder_map[stream_id]);

            if (prev_type != new_type)
            {
                LOGGER__MODULE__INFO(MODULE_NAME, "Encoder type changed for stream {} - restart required", stream_id);
                restart_required |= true;
                break;
            }
        }
    }
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

    bool denoise_bayer_changed = new_profile.iq_settings.denoise.bayer != previous_profile.iq_settings.denoise.bayer;
    bool hdr_changed = new_profile.iq_settings.hdr.enabled != previous_profile.iq_settings.hdr.enabled;
    // only SDR<->HDR change handled by isp manager
    if (hdr_changed && !denoise_bayer_changed && !new_profile.iq_settings.denoise.bayer &&
        IspManager::is_fast_toggle_supported())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "HDR setting changed, fully handled by isp manager");
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
    pause_required |= prev_sensor.input_video.sensor_id != new_sensor.input_video.sensor_id;
    pause_required |= prev_sensor.sensor_calibration_file_path != new_sensor.sensor_calibration_file_path;
    pause_required |= prev_sensor.sensor_configuration != new_sensor.sensor_configuration;
    pause_required |= prev_sensor.input_video.sensor_id != new_sensor.input_video.sensor_id;

    pause_required |= denoise_bayer_changed;
    return pause_required;
}

media_library_return MediaLibrary::set_profile(const std::string &profile_name)
{
    std::lock_guard<std::recursive_mutex> lock(m_profile_change_mutex);
    LOGGER__MODULE__INFO(MODULE_NAME, "Setting profile to {}", profile_name);
    // verify that profile_name exists in medialib_config
    auto medialib_config_exp = m_config_manager_interactor->get_medialib_config();
    if (!medialib_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return medialib_config_exp.error();
    }
    auto profiles = medialib_config_exp.value().profile_by_name;
    if (profiles.find(profile_name) == profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Profile name '{}' does not exist in medialib_config", profile_name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (m_override_persistent_settings)
    {
        auto previous_profile_opt = m_config_manager_interactor->get_current_profile();
        auto &previous_profile = previous_profile_opt.value();
        profiles[profile_name]->override(*previous_profile);
    }

    media_library_return status = set_override_parameters(*profiles[profile_name]);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Failed to set profile to {}", profile_name);
        return status;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Profile set to {}", profile_name);
    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibrary::set_override_persistent_settings(bool override_persistent_settings)
{
    m_override_persistent_settings = override_persistent_settings;
}

media_library_return MediaLibrary::reset_profiles()
{
    std::lock_guard<std::recursive_mutex> lock(m_profile_change_mutex);
    auto current_profile_without_overriden_params_opt =
        m_config_manager_interactor->get_current_profile_without_overriden_params();
    if (!current_profile_without_overriden_params_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile without overridden parameters");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    set_override_parameters(
        *current_profile_without_overriden_params_opt.value()); // to make sure pause/unpause/restart is handled
    auto ret = m_config_manager_interactor->reset_profiles();
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to revert profiles overrides in config manager");
        return ret;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<config_profile_t, media_library_return> MediaLibrary::get_profile(const std::string &profile_name)
{
    auto medialib_config_exp = m_config_manager_interactor->get_medialib_config();
    if (!medialib_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return tl::unexpected(medialib_config_exp.error());
    }
    auto profiles = medialib_config_exp.value().profile_by_name;
    auto it = profiles.find(profile_name);
    if (it != profiles.end())
    {
        return *it->second;
    }

    return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
}

tl::expected<config_profile_t, media_library_return> MediaLibrary::get_current_profile()
{
    // TODO: sync encoder config sync frontend config
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No current profile is set in the configuration");
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }
    return *current_profile_opt.value();
}

tl::expected<std::string, media_library_return> MediaLibrary::get_current_profile_str()
{
    auto current_profile_res = get_current_profile();
    if (!current_profile_res.has_value())
    {
        return tl::unexpected(current_profile_res.error());
    }
    std::string profile_string = m_config_manager_interactor->profile_struct_to_string(current_profile_res.value());
    return profile_string;
}

media_library_return MediaLibrary::subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks)
{
    m_frontend->subscribe(fe_callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::subscribe_to_encoder_output(output_stream_id_t streamId, AppWrapperCallback callback)
{
    auto it = m_encoders.find(streamId);
    if (it == m_encoders.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder for stream '{}' does not exist", streamId);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    m_encoders[streamId]->subscribe(callback);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::start_pipeline()
{
    if (m_config_manager_interactor->get_restricted_profile_type() ==
        restricted_profile_type_t::RESTICTED_PROFILE_STREAMING)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Pipeline in critical thermal state - streaming is disabled");
        return MEDIA_LIBRARY_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "roceeding to start Pipeline");
    return start_pipeline_internal();
}

media_library_return MediaLibrary::start_pipeline_internal()
{
    if (m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Start pipeline request Failed: Pipeline is not initialized");
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_pipeline_state == media_library_pipeline_state_t::PIPELINE_STATE_RUNNING)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Start pipeline request: Pipeline is already running");
        return MEDIA_LIBRARY_SUCCESS;
    }

    std::unique_lock<std::recursive_mutex> lock(m_mutex);

    if (m_throttling_monitor && m_throttling_monitor_user_id != INVALID_THROTTLING_MONITOR_USER_ID)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Throttling monitor already started with user_id: {}",
                              m_throttling_monitor_user_id);
    }
    else if (m_throttling_monitor)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Re-initializing throttling monitor after restart");
        auto thermal_result = initialize_thermal_throttling_monitor();
        if (thermal_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to re-initialize throttling monitor");
            return thermal_result;
        }
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Throttling monitor is not initialized, skipping start");
    }

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
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
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
    if (m_throttling_monitor && m_throttling_monitor_user_id != INVALID_THROTTLING_MONITOR_USER_ID)
    {
        m_throttling_monitor->stop(m_throttling_monitor_user_id);
        m_throttling_monitor_user_id = INVALID_THROTTLING_MONITOR_USER_ID;
        m_throttling_monitor = nullptr;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Destroying MediaLibrary instance");
    m_frontend = nullptr;
    m_encoders.clear();

    auto symlink_3aconfig_opt = m_config_manager_interactor->get_isp_3a_config_symlink_path();
    if (symlink_3aconfig_opt.has_value())
    {
        safe_remove_symlink_target(symlink_3aconfig_opt.value());
    }

    auto symlink_sensor_opt = m_config_manager_interactor->get_isp_sensor_symlink_path();
    if (symlink_sensor_opt.has_value())
    {
        safe_remove_symlink_target(symlink_sensor_opt.value());
    }
}

media_library_return MediaLibrary::shutdown()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Shutting down MediaLibrary");

    if (m_throttling_monitor && m_throttling_monitor_user_id != INVALID_THROTTLING_MONITOR_USER_ID)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Stopping throttling monitor for user_id: {}", m_throttling_monitor_user_id);
        auto status = m_throttling_monitor->stop(m_throttling_monitor_user_id);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop throttling monitor");
            return status;
        }
        m_throttling_monitor_user_id = INVALID_THROTTLING_MONITOR_USER_ID;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary shutdown complete");
    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibrary::set_default_backup_folder_path(const std::string &path)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_default_backup_folder_path = path;
    LOGGER__MODULE__INFO(MODULE_NAME, "Set default backup folder path to: {}", path);
}

media_library_return MediaLibrary::set_restriction_fallback_profile(const std::string &profile_name)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_restriction_fallback_profile = profile_name;
    LOGGER__MODULE__INFO(MODULE_NAME, "Set restriction fallback profile to: {}", profile_name);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::backup_profiles()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_default_backup_folder_path.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No backup folder path specified");
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Backing up profiles to: {}", m_default_backup_folder_path);
    return m_config_manager_interactor->backup_profiles(m_default_backup_folder_path);
}
