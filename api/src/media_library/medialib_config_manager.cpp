#include <iostream>
#include <string>
#include <vector>
#include "media_library/medialib_config_manager.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "media_library/config_manager.hpp"
#include "media_library/logger_macros.hpp"
#include <nlohmann/json.hpp>

#define MODULE_NAME LoggerType::Config
using json = nlohmann::json;

MediaLibConfigManager::MediaLibConfigManager()
    : m_medialib_config_manager(ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG),
      m_profile_config_manager(ConfigSchema::CONFIG_SCHEMA_PROFILE),
      m_frontend_config_manager(ConfigSchema::CONFIG_SCHEMA_FRONTEND),
      m_encoder_config_manager(ConfigSchema::CONFIG_SCHEMA_ENCODER)
{
    m_restricted_profile_type = restricted_profile_type_t::RESTICTED_PROFILE_NONE;
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

bool MediaLibConfigManager::is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type)
{
    ConfigManager config_manager = ConfigManager(config_schema_type);
    return config_manager.is_valid_configuration(config_string);
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
    return validate_configuration(read_string_from_file(m_current_profile.isp_config_files.aaa_config_path),
                                  ConfigSchema::CONFIG_SCHEMA_AAACONFIG);
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

ProfileConfig MediaLibConfigManager::get_profile(const std::string &profile_name)
{
    return m_medialib_config.profiles[profile_name];
}

ProfileConfig MediaLibConfigManager::get_default_profile()
{
    return m_medialib_config.profiles[m_medialib_config.default_profile];
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

ProfileConfig MediaLibConfigManager::set_frontend_config(frontend_config_t frontend_config)
{
    m_current_profile.input_config = frontend_config.input_config;
    m_current_profile.ldc_config = frontend_config.ldc_config;
    m_current_profile.denoise_config = frontend_config.denoise_config;
    m_current_profile.multi_resize_config = frontend_config.multi_resize_config;
    m_current_profile.hdr_config = frontend_config.hdr_config;
    m_current_profile.hailort_config = frontend_config.hailort_config;
    m_current_profile.isp_config = frontend_config.isp_config;
    return m_current_profile;
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

std::string MediaLibConfigManager::get_3a_config_as_string()
{
    // use the schema to test if this is the old or new 3A config and then copy or dump it
    std::string aaa_config_string = read_string_from_file(m_current_profile.isp_config_files.aaa_config_path);
    if (is_valid_configuration(aaa_config_string, ConfigSchema::CONFIG_SCHEMA_OLD_AAACONFIG))
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "DEPRECATION WARNING: The 3A config file is in the old format");
        return aaa_config_string;
    }
    else if (!is_valid_configuration(aaa_config_string, ConfigSchema::CONFIG_SCHEMA_AAACONFIG))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid 3A config: does not conform to any recognized schema");
    }
    json aaa_config_json = json::parse(aaa_config_string, nullptr, false);

    ConfigManager m_aaa_config_manager(ConfigSchema::CONFIG_SCHEMA_AAACONFIG);
    aaa_config_t aaa_config;
    m_aaa_config_manager.config_string_to_struct<aaa_config_t>(aaa_config_json.dump(), aaa_config);

    auto isp_format_aaa_config = isp_format_aaa_config_t::initialize(aaa_config.automatic_algorithms_config);

    ConfigManager isp_format_aaa_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_NONE);
    std::string isp_format_json =
        isp_format_aaa_config_manager.config_struct_to_string<isp_format_aaa_config_t>(isp_format_aaa_config);

    return isp_format_json;
}
