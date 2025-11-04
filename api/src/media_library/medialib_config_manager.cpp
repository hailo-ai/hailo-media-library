#include "media_library/medialib_config_manager.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/logger_macros.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/sensor_registry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <ranges>
#include <optional>
#include <string>
#include <map>
#include <set>

#define MODULE_NAME LoggerType::Config
using json = nlohmann::json;

MediaLibConfigManagerCore::MediaLibConfigManagerCore()
    : m_medialib_config_manager(ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG),
      m_profile_config_manager(ConfigSchema::CONFIG_SCHEMA_PROFILE),
      m_frontend_config_manager(ConfigSchema::CONFIG_SCHEMA_FRONTEND),
      m_encoder_config_manager(ConfigSchema::CONFIG_SCHEMA_ENCODER_AND_BLENDING)
{
}

MediaLibConfigManagerCore::~MediaLibConfigManagerCore()
{
}

media_library_return MediaLibConfigManagerCore::validate_configuration(std::string config_string,
                                                                       ConfigSchema config_schema_type)
{
    ConfigManager config_manager = ConfigManager(config_schema_type);
    media_library_return validation_status = config_manager.validate_configuration(config_string);
    if (validation_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Configuration validation failed");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibConfigManagerCore::is_valid_configuration(const std::string &config_string,
                                                       ConfigSchema config_schema_type)
{
    ConfigManager config_manager = ConfigManager(config_schema_type);
    return config_manager.is_valid_configuration(config_string);
}

media_library_return MediaLibConfigManagerCore::configure_medialib(std::string medialib_json_config_string, size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_medialib_json_config_strings.find(idx) == m_medialib_json_config_strings.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (!medialib_json_config_string.empty() && (medialib_json_config_string == m_medialib_json_config_strings[idx]))
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    m_medialib_json_config_strings[idx] = medialib_json_config_string;
    if (validate_configuration(medialib_json_config_string, ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate media library config");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    medialib_config_t medialib_config;
    auto status = m_medialib_config_manager.config_string_to_struct<medialib_config_t>(medialib_json_config_string,
                                                                                       medialib_config);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse medialib config json string");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    status = m_medialib_configs[idx].set(medialib_config);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse medialib config");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    m_current_profiles[idx] = m_medialib_configs[idx].profiles[m_medialib_configs[idx].default_profile];
    m_medialib_json_config_strings[idx] = medialib_json_config_string;

    // Validate configuration restrictions
    media_library_return sensor_validation = validate_sensor_index_uniqueness();
    if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate sensor index uniqueness");
        return sensor_validation;
    }

    return validate_multi_instance_restrictions();
}

media_library_return MediaLibConfigManagerCore::set_profile(std::string profile, size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_medialib_configs.find(idx) == m_medialib_configs.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    auto it = m_medialib_configs[idx].profiles.find(profile);
    if (it != m_medialib_configs[idx].profiles.end())
    {
        m_current_profiles[idx] = it->second;

        // Validate configuration restrictions
        media_library_return sensor_validation = validate_sensor_index_uniqueness();
        if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate sensor index uniqueness when setting profile");
            return sensor_validation;
        }

        media_library_return multi_instance_validation = validate_multi_instance_restrictions();
        if (multi_instance_validation != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate multi instance restrictions when setting profile");
            return multi_instance_validation;
        }
    }
    else
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Profile '{}' does not exist in medialib_config", profile);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

std::optional<config_profile_t> MediaLibConfigManagerCore::get_profile(const std::string &profile_name, size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_medialib_configs.find(idx) == m_medialib_configs.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }
    return m_medialib_configs[idx].profiles[profile_name];
}

std::string MediaLibConfigManager::profile_struct_to_string(config_profile_t profile)
{
    ConfigManager profile_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_PROFILE);
    std::string profile_string = profile_config_manager.config_struct_to_string<config_profile_t>(profile);
    return profile_string;
}

std::optional<config_profile_t> MediaLibConfigManagerCore::get_default_profile(size_t idx)
{
    return get_profile(m_medialib_configs[idx].default_profile, idx);
}

media_library_return MediaLibConfigManagerCore::set_profile(config_profile_t profile, size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    m_current_profiles[idx] = profile;

    // Validate configuration restrictions
    media_library_return sensor_validation = validate_sensor_index_uniqueness();
    if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
    {
        return sensor_validation;
    }

    media_library_return multi_instance_validation = validate_multi_instance_restrictions();
    if (multi_instance_validation != MEDIA_LIBRARY_SUCCESS)
    {
        return multi_instance_validation;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

std::optional<frontend_config_t> MediaLibConfigManagerCore::get_frontend_config(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }
    frontend_config_t frontend = m_current_profiles[idx].to_frontend_config();
    return frontend;
}

std::optional<config_profile_t> MediaLibConfigManagerCore::set_frontend_config(frontend_config_t frontend_config,
                                                                               size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }
    m_current_profiles[idx].from_frontend_config(frontend_config);

    // Validate configuration restrictions
    media_library_return sensor_validation = validate_sensor_index_uniqueness();
    if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Frontend config validation failed: sensor index uniqueness check");
        return std::nullopt;
    }

    media_library_return multi_instance_validation = validate_multi_instance_restrictions();
    if (multi_instance_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Frontend config validation failed: multi-instance restrictions check");
        return std::nullopt;
    }
    return m_current_profiles[idx];
}

std::optional<std::string> MediaLibConfigManagerCore::get_frontend_config_as_string(size_t idx)
{
    auto frontend_config = get_frontend_config(idx);
    if (!frontend_config.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx out of bounds: {}", idx);
        return std::nullopt;
    }
    return m_frontend_config_manager.config_struct_to_string<frontend_config_t>(frontend_config.value());
}

std::optional<std::map<output_stream_id_t, encoder_config_t>> MediaLibConfigManagerCore::get_encoder_configs(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }
    return m_current_profiles[idx].to_encoder_config_map();
}

std::optional<std::map<output_stream_id_t, config_encoded_output_stream_t>> MediaLibConfigManagerCore::
    get_encoded_output_streams(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }
    return m_current_profiles[idx].to_encoded_output_stream_config_map();
}

media_library_return MediaLibConfigManagerCore::get_sensor_entry_config(std::string &sensor_entry, size_t idx)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Entering get_sensor_entry_config with idx: {}", idx);

    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    auto ret = is_sensor_connected_by_name(m_current_profiles[idx].sensor_config.sensor_configuration.name, idx);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid sensor configured for idx: {}", idx);
        return ret;
    }

    auto i2c_info_opt = get_i2c_bus_and_address(idx);
    if (!i2c_info_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get I2C bus and address for idx: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    int i2c_bus = i2c_info_opt.value().first;
    std::string i2c_address_raw = i2c_info_opt.value().second;

    // Convert address from string format (e.g., "001a") to hex format (e.g., "0x1a")
    // Parse the hex value and format it properly without leading zeros
    unsigned long addr_value = std::stoul(i2c_address_raw, nullptr, 16);
    std::stringstream addr_stream;
    addr_stream << "0x" << std::hex << addr_value;
    std::string i2c_address = addr_stream.str();

    LOGGER__MODULE__TRACE(MODULE_NAME, "I2C bus: {}, I2C address: {} for idx: {}", i2c_bus, i2c_address, idx);

    isp_format_config_sensor_configuration_t isp_format_sensor_entry(
        m_current_profiles[idx].iq_settings.hdr.enabled,
        m_current_profiles[idx].sensor_config.sensor_calibration_file_path,
        m_current_profiles[idx].sensor_config.sensor_configuration, i2c_bus, i2c_address);
    ConfigManager config_manager = ConfigManager(CONFIG_SCHEMA_NONE);
    sensor_entry =
        config_manager.config_struct_to_string<isp_format_config_sensor_configuration_t>(isp_format_sensor_entry, 2);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Successfully generated sensor entry config for idx: {}", idx);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibConfigManagerCore::get_3a_config(std::string &aaa_config_string, size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "creating Isp 3a config from current 3a config struct");
    auto isp_format_aaa_config =
        isp_format_aaa_config_t::initialize(m_current_profiles[idx].iq_settings.automatic_algorithms_config);
    ConfigManager isp_format_aaa_config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_NONE);
    std::string isp_format_json =
        isp_format_aaa_config_manager.config_struct_to_string<isp_format_aaa_config_t>(isp_format_aaa_config);
    aaa_config_string = isp_format_json;
    return MEDIA_LIBRARY_SUCCESS;
}

std::optional<std::pair<int, std::string>> MediaLibConfigManagerCore::get_i2c_bus_and_address(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }

    size_t sensor_index = m_current_profiles[idx].sensor_config.input_video.sensor_index;

    auto &registry = SensorRegistry::get_instance();
    return registry.get_i2c_bus_and_address(sensor_index);
}

std::optional<SensorType> MediaLibConfigManagerCore::get_sensor_type(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }

    size_t sensor_index = m_current_profiles[idx].sensor_config.input_video.sensor_index;

    auto &registry = SensorRegistry::get_instance();
    return registry.detect_sensor_type(sensor_index);
}

std::optional<std::string> MediaLibConfigManagerCore::get_connected_sensor_name(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Media library idx not found: {}", idx);
        return std::nullopt;
    }

    size_t sensor_index = m_current_profiles[idx].sensor_config.input_video.sensor_index;

    auto &registry = SensorRegistry::get_instance();
    return registry.detect_sensor_type_str(sensor_index);
}

media_library_return MediaLibConfigManagerCore::is_sensor_connected_by_name(const std::string &sensor_type_str,
                                                                            size_t idx)
{
    auto sensor_type_opt = get_connected_sensor_name(idx);
    if (!sensor_type_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get sensor type for idx: {}", idx);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    std::string &sensor_type = sensor_type_opt.value();
    LOGGER__MODULE__TRACE(MODULE_NAME, "Successfully retrieved sensor type: {} for idx: {}", sensor_type, idx);

    bool sensor_compare_without_case_sensitivity = std::ranges::equal(
        sensor_type_str, sensor_type, [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
    if (!sensor_compare_without_case_sensitivity)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Sensor type mismatch for idx: {}. Detected sensor: {}, Configured sensor: {}", idx,
                              sensor_type, sensor_type_str);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibConfigManagerCore::initialize_instance(size_t idx)
{
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);
    if (m_medialib_configs.size() >= MAX_INSTANCES)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Cannot initialize index {}: MAX_INSTANCES {} already reached", idx,
                              MAX_INSTANCES);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Initialize default entries for this idx if they don't exist
    if (m_medialib_json_config_strings.find(idx) == m_medialib_json_config_strings.end())
    {
        m_medialib_json_config_strings[idx] = "";
    }
    if (m_medialib_configs.find(idx) == m_medialib_configs.end())
    {
        m_medialib_configs[idx] = MediaLibraryConfig{};
    }
    if (m_current_profiles.find(idx) == m_current_profiles.end())
    {
        m_current_profiles[idx] = config_profile_t{};
    }
    if (m_restricted_profile_types.find(idx) == m_restricted_profile_types.end())
    {
        m_restricted_profile_types[idx] = restricted_profile_type_t::RESTICTED_PROFILE_NONE;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibConfigManagerCore::cleanup_instance(size_t idx)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Cleaning up MediaLibConfigManagerCore instance data for index {}", idx);
    std::lock_guard<std::recursive_mutex> lock(m_maps_mutex);

    // Remove the data for this specific index from all maps
    m_medialib_json_config_strings.erase(idx);
    m_medialib_configs.erase(idx);
    m_current_profiles.erase(idx);
    m_restricted_profile_types.erase(idx);
}

// MediaLibConfigManager implementations
MediaLibConfigManager::MediaLibConfigManager(size_t idx, MediaLibConfigManagerCore &medialib_config_manager_core)
    : m_idx(idx), m_medialib_config_manager_core(medialib_config_manager_core)
{
}

MediaLibConfigManager::~MediaLibConfigManager()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibConfigManager destructor called for index {}", m_idx);
    m_medialib_config_manager_core.cleanup_instance(m_idx);
}

media_library_return MediaLibConfigManager::initialize()
{
    return m_medialib_config_manager_core.initialize_instance(m_idx);
}

media_library_return MediaLibConfigManager::validate_configuration(std::string config_string,
                                                                   ConfigSchema config_schema_type)
{
    return m_medialib_config_manager_core.validate_configuration(config_string, config_schema_type);
}

bool MediaLibConfigManager::is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type)
{
    return m_medialib_config_manager_core.is_valid_configuration(config_string, config_schema_type);
}

media_library_return MediaLibConfigManager::configure_medialib(std::string medialib_json_config_string)
{
    return m_medialib_config_manager_core.configure_medialib(medialib_json_config_string, m_idx);
}

media_library_return MediaLibConfigManager::set_profile(std::string profile)
{
    return m_medialib_config_manager_core.set_profile(profile, m_idx);
}

media_library_return MediaLibConfigManager::set_profile(config_profile_t profile)
{
    return m_medialib_config_manager_core.set_profile(profile, m_idx);
}

config_profile_t MediaLibConfigManager::set_frontend_config(frontend_config_t frontend_config)
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.set_frontend_config(frontend_config, m_idx).value();
}

config_profile_t MediaLibConfigManager::get_profile(const std::string &profile_name)
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.get_profile(profile_name, m_idx).value();
}

config_profile_t MediaLibConfigManager::get_default_profile()
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.get_default_profile(m_idx).value();
}

frontend_config_t MediaLibConfigManager::get_frontend_config()
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.get_frontend_config(m_idx).value();
}

std::string MediaLibConfigManager::get_frontend_config_as_string()
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.get_frontend_config_as_string(m_idx).value();
}

std::map<output_stream_id_t, encoder_config_t> MediaLibConfigManager::get_encoder_configs()
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.get_encoder_configs(m_idx).value();
}

std::map<output_stream_id_t, config_encoded_output_stream_t> MediaLibConfigManager::get_encoded_output_streams()
{
    // Result doesn't have value only if m_idx is out of range, which is not possible
    return m_medialib_config_manager_core.get_encoded_output_streams(m_idx).value();
}

std::optional<std::pair<int, std::string>> MediaLibConfigManager::get_i2c_bus_and_address()
{
    return m_medialib_config_manager_core.get_i2c_bus_and_address(m_idx);
}

std::optional<SensorType> MediaLibConfigManager::get_sensor_type()
{
    return m_medialib_config_manager_core.get_sensor_type(m_idx);
}

media_library_return MediaLibConfigManager::get_sensor_entry_config(std::string &sensor_entry_string)
{
    return m_medialib_config_manager_core.get_sensor_entry_config(sensor_entry_string, m_idx);
}

media_library_return MediaLibConfigManager::get_3a_config(std::string &aaa_config_string)
{
    return m_medialib_config_manager_core.get_3a_config(aaa_config_string, m_idx);
}

restricted_profile_type_t MediaLibConfigManager::get_restricted_profile_type()
{
    std::lock_guard<std::recursive_mutex> lock(m_medialib_config_manager_core.m_maps_mutex);
    return m_medialib_config_manager_core.m_restricted_profile_types[m_idx];
}

void MediaLibConfigManager::set_restricted_profile_type(restricted_profile_type_t type)
{
    std::lock_guard<std::recursive_mutex> lock(m_medialib_config_manager_core.m_maps_mutex);
    m_medialib_config_manager_core.m_restricted_profile_types[m_idx] = type;
}

config_profile_t MediaLibConfigManager::get_current_profile()
{
    std::lock_guard<std::recursive_mutex> lock(m_medialib_config_manager_core.m_maps_mutex);
    return m_medialib_config_manager_core.m_current_profiles[m_idx];
}

MediaLibraryConfig MediaLibConfigManager::get_medialib_config()
{
    std::lock_guard<std::recursive_mutex> lock(m_medialib_config_manager_core.m_maps_mutex);
    return m_medialib_config_manager_core.m_medialib_configs[m_idx];
}

std::string MediaLibConfigManager::get_isp_sensor_symlink_path()
{
    std::lock_guard<std::recursive_mutex> lock(m_medialib_config_manager_core.m_maps_mutex);
    size_t sensor_index =
        m_medialib_config_manager_core.m_current_profiles[m_idx].sensor_config.input_video.sensor_index;
    return "/usr/bin/isp_sensor_" + std::to_string(sensor_index) + "_entry";
}

std::string MediaLibConfigManager::get_isp_3a_config_symlink_path()
{
    std::lock_guard<std::recursive_mutex> lock(m_medialib_config_manager_core.m_maps_mutex);
    size_t sensor_index =
        m_medialib_config_manager_core.m_current_profiles[m_idx].sensor_config.input_video.sensor_index;
    return "/usr/bin/isp_3aconfig_" + std::to_string(sensor_index);
}

media_library_return MediaLibConfigManagerCore::validate_sensor_index_uniqueness()
{
    // Only validate if there are multiple instances
    if (m_current_profiles.size() <= 1)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    std::set<size_t> sensor_indices;

    for (const auto &[idx, profile] : m_current_profiles)
    {
        size_t sensor_index = profile.sensor_config.input_video.sensor_index;

        // Check if sensor_index is already used
        if (sensor_indices.find(sensor_index) != sensor_indices.end())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Duplicate sensor_index {} found in configuration at index {}",
                                  sensor_index, idx);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        sensor_indices.insert(sensor_index);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibConfigManagerCore::validate_multi_instance_restrictions()
{
    // Only validate if there are multiple instances
    if (m_current_profiles.size() <= 1)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    for (const auto &[idx, profile] : m_current_profiles)
    {
        // Check if EIS is enabled
        if (profile.stabilizer_settings.eis.enabled)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "EIS is enabled in configuration at index {} but multiple instances are active. EIS "
                                  "must be disabled when using multiple instances.",
                                  idx);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        // Check if HDR is enabled
        if (profile.iq_settings.hdr.enabled)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "HDR is enabled in configuration at index {} but multiple instances are active. HDR "
                                  "must be disabled when using multiple instances.",
                                  idx);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        // Check if pre-ISP denoise is enabled
        if (profile.iq_settings.denoise.enabled && profile.iq_settings.denoise.bayer &&
            !profile.iq_settings.denoise.bayer_network_config.dgain_channel.empty())
        {
            LOGGER__MODULE__ERROR(
                MODULE_NAME,
                "Pre-ISP denoise is enabled in configuration at index {} but multiple instances are active. "
                "Pre-ISP denoise must be disabled when using multiple instances.",
                idx);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}
