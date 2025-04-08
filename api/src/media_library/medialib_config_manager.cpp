#include <iostream>
#include <string>
#include <vector>
#include "media_library/medialib_config_manager.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "media_library/config_manager.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

MediaLibConfigManager::MediaLibConfigManager()
    : m_medialib_config_manager(ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG),
      m_profile_config_manager(ConfigSchema::CONFIG_SCHEMA_PROFILE),
      m_frontend_config_manager(ConfigSchema::CONFIG_SCHEMA_FRONTEND),
      m_encoder_config_manager(ConfigSchema::CONFIG_SCHEMA_ENCODER)
{
}

MediaLibConfigManager::~MediaLibConfigManager()
{
}

media_library_return MediaLibConfigManager::validate_configuration(std::string config_string,
                                                                   ConfigSchema config_schema_type)
{
    ConfigManager config_manager = ConfigManager(config_schema_type);
    media_library_return validation_status = config_manager.validate_configuration(config_string);
    if (validation_status != MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibConfigManager::configure_medialib(std::string medialib_json_config_string)
{
    if (!medialib_json_config_string.empty() && (medialib_json_config_string == m_medialib_json_config_string))
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (validate_configuration(medialib_json_config_string, ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    medialib_config_t medialib_config_json;
    m_medialib_config_manager.config_string_to_struct<medialib_config_t>(medialib_json_config_string,
                                                                         medialib_config_json);
    m_medialib_config = medialib_config_json;
    m_current_profile = m_medialib_config.profiles[m_medialib_config.default_profile];
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibConfigManager::set_profile(std::string profile)
{
    auto it = m_medialib_config.profiles.find(profile);
    if (it != m_medialib_config.profiles.end())
    {
        m_current_profile = it->second;
    }
    else
    {
        // TODO: prfole not found
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibConfigManager::set_profile(ProfileConfig profile)
{
    if (profile.verify_profile_schema())
        m_current_profile = profile;
    else
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    return MEDIA_LIBRARY_SUCCESS;
}

frontend_config_t MediaLibConfigManager::get_frontend_config()
{
    frontend_config_t frontend_config;
    frontend_config.input_config = m_current_profile.input_config;
    frontend_config.ldc_config = m_current_profile.ldc_config;
    frontend_config.denoise_config = m_current_profile.denoise_config;
    frontend_config.multi_resize_config = m_current_profile.multi_resize_config;
    frontend_config.hdr_config = m_current_profile.hdr_config;
    frontend_config.hailort_config = m_current_profile.hailort_config;
    frontend_config.isp_config = m_current_profile.isp_config;
    return frontend_config;
}

std::string MediaLibConfigManager::get_frontend_config_as_string()
{
    return m_frontend_config_manager.config_struct_to_string<frontend_config_t>(get_frontend_config());
}

std::map<output_stream_id_t, encoder_config_t> MediaLibConfigManager::get_encoder_configs()
{
    return m_current_profile.encoder_configs;
}

std::string MediaLibConfigManager::get_3a_config()
{
    return m_current_profile.isp_config_files.aaa_config_path;
}

std::string MediaLibConfigManager::get_sensor_entry()
{
    return m_current_profile.isp_config_files.sensor_entry_path;
}
