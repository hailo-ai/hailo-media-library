#include "config_manager.hpp"
#include "config_parser.hpp"
#include "config_backup.hpp"
#include "logger_macros.hpp"
#include "media_library_types.hpp"
#include "sensor_registry.hpp"
#include "media_library_rule_checker.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <ranges>
#include <optional>
#include <string>
#include <map>
#include <cmath>
#include <climits>
#include <tl/expected.hpp>

#define MODULE_NAME LoggerType::Config
using json = nlohmann::json;

ConfigManager::ConfigManager() = default;
ConfigManager::~ConfigManager() = default;

media_library_return ConfigManager::validate_metadata(const std::string &config_string)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Validating configuration metadata");
    ConfigParser config_manager = ConfigParser(ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG);
    media_library_return validation_status = config_manager.validate_config_metadata(config_string);
    if (validation_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Configuration metadata validation failed");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

bool ConfigManager::is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type)
{
    ConfigParser config_manager = ConfigParser(config_schema_type);
    return config_manager.is_valid_configuration(config_string);
}

media_library_return ConfigManager::set_medialib_config_in_gst_mode(ConfigManagerInteractor *interactor,
                                                                    const MediaLibraryConfig &medialib_config)
{
    // should not fail, the creation process of interactor prevents it to exists without registration
    assert(m_interactor_to_hml_config.find(interactor) != m_interactor_to_hml_config.end());

    m_interactor_to_hml_config[interactor] = medialib_config;

    // Validate configuration restrictions
    media_library_return sensor_validation = validate_sensor_index_uniqueness();
    if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to validate sensor index uniqueness", (void *)interactor);
        return sensor_validation;
    }

    return validate_multi_instance_restrictions();
}

media_library_return ConfigManager::set_medialib_config(ConfigManagerInteractor *interactor,
                                                        const std::string &medialib_json_config_string, bool force)
{
    // should not fail, the creation process of interactor prevents it to exists without registration
    assert(m_interactor_to_hml_config.find(interactor) != m_interactor_to_hml_config.end());

    if (!medialib_json_config_string.empty() &&
        (medialib_json_config_string == m_interactor_to_hml_config[interactor].hml_json_config) && !force)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    if (validate_configuration(medialib_json_config_string, ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to validate media library config", (void *)interactor);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (validate_metadata(medialib_json_config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to validate media library config metadata", (void *)interactor);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    auto status = m_interactor_to_hml_config[interactor].set(medialib_json_config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to parse medialib config json string", (void *)interactor);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Validate configuration restrictions
    media_library_return sensor_validation = validate_sensor_index_uniqueness();
    if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to validate sensor index uniqueness", (void *)interactor);
        return sensor_validation;
    }

    return validate_multi_instance_restrictions();
}

media_library_return ConfigManager::switch_to_profile_by_name(const ConfigManagerInteractor *interactor,
                                                              const std::string &profile)
{
    // should not fail, the creation process of interactor prevents it to exists without registration
    assert(m_interactor_to_hml_config.find(interactor) != m_interactor_to_hml_config.end());

    if (m_interactor_to_hml_config[interactor].set_profile_in_use(profile) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to set profile in use to '{}'", (void *)interactor, profile);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Validate configuration restrictions
    media_library_return sensor_validation = validate_sensor_index_uniqueness();
    if (sensor_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to validate sensor index uniqueness when setting profile",
                              (void *)interactor);
        return sensor_validation;
    }

    media_library_return multi_instance_validation = validate_multi_instance_restrictions();
    if (multi_instance_validation != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to validate multi instance restrictions when setting profile",
                              (void *)interactor);
        return multi_instance_validation;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<MediaLibraryConfig, media_library_return> ConfigManager::get_medialib_config(
    const ConfigManagerInteractor *interactor)
{
    if (m_interactor_to_hml_config.find(interactor) == m_interactor_to_hml_config.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Interactor not registered in ConfigManager", (void *)interactor);
        return tl::unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    return m_interactor_to_hml_config[interactor];
}

void ConfigManager::set_restricted_profile_type(const ConfigManagerInteractor *interactor,
                                                restricted_profile_type_t restricted_profile_type)
{
    // should not fail, the creation process of interactor prevents it to exists without registration
    assert(m_interactor_to_hml_config.find(interactor) != m_interactor_to_hml_config.end());

    m_interactor_to_hml_config[interactor].current_restriction = restricted_profile_type;
}

media_library_return ConfigManager::reset_profiles_overriden_params(const ConfigManagerInteractor *interactor)
{
    assert(m_interactor_to_hml_config.find(interactor) != m_interactor_to_hml_config.end());

    auto status = m_interactor_to_hml_config[interactor].reset_overriden_params();
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to parse medialib config json string", (void *)interactor);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

bool ConfigManager::has_interactors()
{
    std::lock_guard<std::mutex> lock(registration_mutex);
    return !m_interactor_to_hml_config.empty();
}

std::optional<std::shared_ptr<const config_profile_t>> ConfigManagerInteractor::get_profile_by_name(
    const std::string &profile_name) const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return std::nullopt;
    }
    auto &hml_config = hml_config_exp.value();
    auto it = hml_config.profile_by_name.find(profile_name);
    if (it != hml_config.profile_by_name.end())
    {
        return it->second;
    }
    LOGGER__MODULE__ERROR(MODULE_NAME, "Profile '{}' does not exist in medialib_config", profile_name);
    return std::nullopt;
}

std::string ConfigManagerInteractor::profile_struct_to_string(const config_profile_t &profile)
{
    ConfigParser profile_config_manager = ConfigParser(ConfigSchema::CONFIG_SCHEMA_PROFILE);
    std::string profile_string = profile_config_manager.config_struct_to_string<config_profile_t>(profile);
    return profile_string;
}

media_library_return ConfigManager::set_profile(const ConfigManagerInteractor *interactor,
                                                const config_profile_t &profile)
{
    // should not fail, the creation process of interactor prevents it to exists without registration
    assert(m_interactor_to_hml_config.find(interactor) != m_interactor_to_hml_config.end());

    m_interactor_to_hml_config[interactor].profile_by_name[profile.name] = std::make_shared<config_profile_t>(profile);
    if (m_interactor_to_hml_config[interactor].set_profile_in_use(profile.name) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to set profile in use to '{}'", (void *)interactor,
                              profile.name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

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

media_library_return ConfigManager::validate_profile_rules(ConfigManagerInteractor *interactor,
                                                           const std::string &profile_json_config_str)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "[{}] Validating profile against rules", (void *)interactor);
    MediaLibraryRuleChecker rule_checker;
    auto status = rule_checker.validate_config(nlohmann::json::parse(profile_json_config_str));
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Rule checker validation failed for profile", (void *)interactor);
        return status;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManagerInteractor::reset_profiles()
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "[{}] Resetting profiles overrides, setting default medialib config",
                          (void *)this);
    if (ConfigManager::get_instance().reset_profiles_overriden_params(this) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to set medialib config during reset", (void *)this);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

std::optional<std::string> ConfigManagerInteractor::get_connected_sensor_name() const
{
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return std::nullopt;
    }
    auto &hml_config = hml_config_exp.value();
    size_t sensor_index = hml_config.get_profile_in_use()->sensor_config.input_video.sensor_id;

    auto &registry = SensorRegistry::get_instance();
    return registry.detect_sensor_type_str(sensor_index);
}

media_library_return ConfigManagerInteractor::is_sensor_connected_by_name(const std::string &sensor_type_str) const
{
    auto sensor_type_opt = get_connected_sensor_name();
    if (!sensor_type_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to get sensor type", (void *)this);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    std::string &sensor_type = sensor_type_opt.value();
    LOGGER__MODULE__TRACE(MODULE_NAME, "[{}] Successfully retrieved sensor type: {}", (void *)this, sensor_type);

    bool sensor_compare_without_case_sensitivity = std::ranges::equal(
        sensor_type_str, sensor_type, [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
    if (!sensor_compare_without_case_sensitivity)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Sensor type mismatch for Detected sensor: {}, Configured sensor: {}",
                              (void *)this, sensor_type, sensor_type_str);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

ConfigManager &ConfigManager::get_instance()
{
    static ConfigManager instance;
    return instance;
}

media_library_return ConfigManager::register_interactor(ConfigManagerInteractor *interactor)
{
    std::lock_guard<std::mutex> lock(registration_mutex);
    if (m_interactor_to_hml_config.size() >= MAX_INSTANCES)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Cannot initialize interactor. MAX_INSTANCES {} already reached",
                              (void *)interactor, MAX_INSTANCES);
        return MEDIA_LIBRARY_ERROR;
    }

    // Initialize default entries for this interactor if they don't exist
    if (m_interactor_to_hml_config.find(interactor) == m_interactor_to_hml_config.end())
    {
        m_interactor_to_hml_config[interactor] = MediaLibraryConfig{};
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "[{}] ConfigManager interactor registered successfully", (void *)interactor);
    return MEDIA_LIBRARY_SUCCESS;
}

void ConfigManager::unregister_interactor(ConfigManagerInteractor *interactor)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "[{}] Cleaning up ConfigManager interactor data", (void *)interactor);
    std::lock_guard<std::mutex> lock(registration_mutex);

    // Remove the data for this specific index from all maps
    m_interactor_to_hml_config.erase(interactor);
}

bool ConfigManager::is_dual_sensor()
{
    return (m_interactor_to_hml_config.size() == 2);
}

// ConfigManager implementations
ConfigManagerInteractor::ConfigManagerInteractor()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "[{}] ConfigManagerInteractor constructor called", (void *)this);
}

ConfigManagerInteractor::~ConfigManagerInteractor()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "[{}] ConfigManagerInteractor destructor called", (void *)this);
    ConfigManager::get_instance().unregister_interactor(this);
}

std::mutex ConfigManagerInteractor::interaction_mtx;

tl::expected<std::unique_ptr<ConfigManagerInteractor>, media_library_return> ConfigManagerInteractor::create(
    const std::string &medialib_json_config_string)
{
    std::unique_ptr<ConfigManagerInteractor> interactor =
        std::unique_ptr<ConfigManagerInteractor>(new ConfigManagerInteractor());
    auto res = ConfigManager::get_instance().register_interactor(interactor.get());
    if (res != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to create ConfigManagerInteractor", (void *)interactor.get());
        return tl::unexpected(res);
    }
    std::lock_guard<std::mutex> lock(interaction_mtx);
    if (ConfigManager::get_instance().set_medialib_config(interactor.get(), medialib_json_config_string) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to set medialib config during creation",
                              (void *)interactor.get());
        ConfigManager::get_instance().unregister_interactor(interactor.get());
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }
    return interactor;
}

tl::expected<std::unique_ptr<ConfigManagerInteractor>, media_library_return> ConfigManagerInteractor::
    create_dummy_profile_interactor(const frontend_config_t &frontend_config)
{
    std::unique_ptr<ConfigManagerInteractor> interactor =
        std::unique_ptr<ConfigManagerInteractor>(new ConfigManagerInteractor());
    auto res = ConfigManager::get_instance().register_interactor(interactor.get());
    if (res != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to create ConfigManagerInteractor", (void *)interactor.get());
        return tl::unexpected(res);
    }
    MediaLibraryConfig partial_medialib_config;
    res = partial_medialib_config.set_dummy_profile(frontend_config);
    if (res != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to parse medialib config from frontend config string",
                              (void *)interactor.get());
        ConfigManager::get_instance().unregister_interactor(interactor.get());
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    std::lock_guard<std::mutex> lock(interaction_mtx);
    if (ConfigManager::get_instance().set_medialib_config_in_gst_mode(interactor.get(), partial_medialib_config) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to set medialib config during creation",
                              (void *)interactor.get());
        ConfigManager::get_instance().unregister_interactor(interactor.get());
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }
    return interactor;
}

media_library_return ConfigManager::validate_configuration(const std::string &config_string,
                                                           ConfigSchema config_schema_type)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Validating configuration");
    ConfigParser config_parser = ConfigParser(config_schema_type);
    media_library_return validation_status = config_parser.validate_configuration(config_string);
    if (validation_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Configuration validation failed");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManagerInteractor::switch_to_profile_by_name(const std::string &profile)
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    return ConfigManager::get_instance().switch_to_profile_by_name(this, profile);
}

media_library_return ConfigManagerInteractor::set_profile(const config_profile_t &profile)
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    return ConfigManager::get_instance().set_profile(this, profile);
}

media_library_return ConfigManagerInteractor::validate_profile_rules(const std::string &profile_json_config_str)
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    return ConfigManager::get_instance().validate_profile_rules(this, profile_json_config_str);
}

config_profile_t ConfigManagerInteractor::set_frontend_config(const frontend_config_t &frontend_config)
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return config_profile_t{};
    }
    auto current_profile = hml_config_exp.value().get_profile_in_use();
    auto new_profile = *current_profile;

    new_profile.from_frontend_config(frontend_config);

    ConfigManager::get_instance().set_profile(this, new_profile);
    return new_profile;
}

std::shared_ptr<config_profile_t> ConfigManagerInteractor::get_default_profile() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return nullptr;
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    auto default_profile_name = hml_config.default_profile_name;
    return hml_config.profile_by_name.at(default_profile_name);
}

frontend_config_t ConfigManagerInteractor::get_frontend_config() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return frontend_config_t{};
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    return hml_config.get_profile_in_use()->to_frontend_config();
}

std::string ConfigManagerInteractor::get_frontend_config_as_string() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    ConfigParser frontend_config_manager = ConfigParser(ConfigSchema::CONFIG_SCHEMA_FRONTEND);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return "";
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    auto frontend_config = hml_config.get_profile_in_use()->to_frontend_config();
    std::string frontend_config_string =
        frontend_config_manager.config_struct_to_string<frontend_config_t>(frontend_config);
    return frontend_config_string;
}

std::map<output_stream_id_t, encoder_config_t> ConfigManagerInteractor::get_encoder_configs() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return {};
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    return hml_config.get_profile_in_use()->to_encoder_config_map();
}

std::map<output_stream_id_t, config_encoded_output_stream_t> ConfigManagerInteractor::get_encoded_output_streams() const
{
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address(const MediaLibraryConfig &hml_config);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return {};
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    return hml_config.get_profile_in_use()->encoded_output_streams;
}

std::optional<std::pair<int, std::string>> ConfigManagerInteractor::get_i2c_bus_and_address(
    const MediaLibraryConfig &hml_config) const
{
    size_t sensor_index = hml_config.get_profile_in_use()->sensor_config.input_video.sensor_id;

    auto &registry = SensorRegistry::get_instance();
    return registry.get_i2c_bus_and_address(sensor_index);
}

std::optional<SensorType> ConfigManagerInteractor::get_sensor_type() const
{
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address(const MediaLibraryConfig &hml_config);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return std::nullopt;
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    size_t sensor_index = hml_config.get_profile_in_use()->sensor_config.input_video.sensor_id;

    auto &registry = SensorRegistry::get_instance();
    return registry.detect_sensor_type(sensor_index);
}

std::optional<std::string> ConfigManagerInteractor::get_sensor_entry_config() const
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "[{}] Entering get_sensor_entry_config", (void *)this);
    std::lock_guard<std::mutex> lock(interaction_mtx);

    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return std::nullopt;
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    auto ret = is_sensor_connected_by_name(hml_config.get_profile_in_use()->sensor_config.sensor_configuration.name);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Invalid sensor configured", (void *)this);
        return std::nullopt;
    }

    auto i2c_info_opt = get_i2c_bus_and_address(hml_config);
    if (!i2c_info_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to get I2C bus and address", (void *)this);
        return std::nullopt;
    }
    int i2c_bus = i2c_info_opt.value().first;
    std::string i2c_address_raw = i2c_info_opt.value().second;

    // Convert address from string format (e.g., "001a") to hex format (e.g., "0x1a")
    // Parse the hex value and format it properly without leading zeros
    unsigned long addr_value = std::stoul(i2c_address_raw, nullptr, 16);
    std::stringstream addr_stream;
    addr_stream << "0x" << std::hex << addr_value;
    std::string i2c_address = addr_stream.str();

    LOGGER__MODULE__TRACE(MODULE_NAME, "[{}] I2C bus: {}, I2C address: {}", (void *)this, i2c_bus, i2c_address);

    isp_format_config_sensor_configuration_t isp_format_sensor_entry(
        hml_config.get_profile_in_use()->iq_settings.hdr.enabled,
        hml_config.get_profile_in_use()->sensor_config.sensor_calibration_file_path,
        hml_config.get_profile_in_use()->sensor_config.sensor_configuration, i2c_bus, i2c_address);
    ConfigParser config_manager = ConfigParser(CONFIG_SCHEMA_NONE);
    std::string sensor_entry =
        config_manager.config_struct_to_string<isp_format_config_sensor_configuration_t>(isp_format_sensor_entry, 2);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "[{}] Successfully generated sensor entry config", (void *)this);
    return sensor_entry;
}

std::string ConfigManagerInteractor::get_3a_config() const
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "[{}] creating Isp 3a config from current 3a config struct", (void *)this);
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address(const MediaLibraryConfig &hml_config);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return "";
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    auto isp_format_aaa_config =
        isp_format_aaa_config_t::initialize(hml_config.get_profile_in_use()->iq_settings.automatic_algorithms_config);
    ConfigParser isp_format_aaa_config_manager = ConfigParser(ConfigSchema::CONFIG_SCHEMA_NONE);
    std::string isp_format_json =
        isp_format_aaa_config_manager.config_struct_to_string<isp_format_aaa_config_t>(isp_format_aaa_config);
    std::string aaa_config_string = isp_format_json;
    return aaa_config_string;
}

restricted_profile_type_t ConfigManagerInteractor::get_restricted_profile_type() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return restricted_profile_type_t::RESTICTED_PROFILE_NONE;
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    return hml_config.current_restriction;
}

void ConfigManagerInteractor::set_restricted_profile_type(restricted_profile_type_t restriction_type)
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    ConfigManager::get_instance().set_restricted_profile_type(this, restriction_type);
}

std::optional<std::shared_ptr<const config_profile_t>> ConfigManagerInteractor::get_current_profile() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to get medialib config", (void *)this);
        return std::nullopt;
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    if (!hml_config.get_profile_in_use())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] No profile is currently in use", (void *)this);
        return std::nullopt;
    }
    return hml_config.get_profile_in_use();
}

std::optional<std::shared_ptr<const config_profile_t>> ConfigManagerInteractor::
    get_current_profile_without_overriden_params() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to get medialib config", (void *)this);
        return std::nullopt;
    }
    auto profile_in_use_without_overriden_params_exp =
        hml_config_exp.value().get_profile_in_use_without_overriden_params();
    if (!profile_in_use_without_overriden_params_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] No profile is currently in use", (void *)this);
        return std::nullopt;
    }
    return profile_in_use_without_overriden_params_exp.value();
}

std::optional<std::shared_ptr<const config_profile_t>> ConfigManagerInteractor::get_fallback_profile() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Failed to get medialib config", (void *)this);
        return std::nullopt;
    }
    const MediaLibraryConfig &hml_config = hml_config_exp.value();
    auto fallback_profile_opt = hml_config.get_fallback_profile();
    if (!fallback_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] No profile is currently in use", (void *)this);
        return std::nullopt;
    }
    return hml_config.get_fallback_profile();
}

media_library_return ConfigManagerInteractor::update_encoder_streams_for_rotation() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto current_profile = hml_config_exp.value().get_profile_in_use();
    auto new_profile = *current_profile;

    new_profile.update_encoded_output_streams_rotation();
    return ConfigManager::get_instance().set_profile(this, new_profile);
}

tl::expected<MediaLibraryConfig, media_library_return> ConfigManagerInteractor::get_medialib_config() const
{
    std::lock_guard<std::mutex> lock(interaction_mtx);
    return ConfigManager::get_instance().get_medialib_config(this);
}

std::optional<std::string> ConfigManagerInteractor::get_isp_sensor_symlink_path() const
{
    auto current_profile_opt = get_current_profile();
    if (!current_profile_opt.has_value())
    {
        return std::nullopt;
    }
    size_t sensor_index = current_profile_opt.value()->sensor_config.input_video.sensor_id;
    return "/usr/bin/isp_sensor_" + std::to_string(sensor_index) + "_entry";
}

std::optional<std::string> ConfigManagerInteractor::get_isp_3a_config_symlink_path() const
{
    auto current_profile_opt = get_current_profile();
    if (!current_profile_opt.has_value())
    {
        return std::nullopt;
    }
    size_t sensor_index = current_profile_opt.value()->sensor_config.input_video.sensor_id;
    return "/usr/bin/isp_3aconfig_" + std::to_string(sensor_index);
}

bool ConfigManagerInteractor::is_dual_sensor() const
{
    return ConfigManager::get_instance().is_dual_sensor();
}

media_library_return ConfigManager::validate_sensor_index_uniqueness()
{
    // Only validate if there are multiple instances
    if (m_interactor_to_hml_config.size() <= 1)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    std::set<size_t> sensor_indices;

    for (const auto &[interactor, hml_config] : m_interactor_to_hml_config)
    {
        size_t sensor_index = hml_config.get_profile_in_use()->sensor_config.input_video.sensor_id;

        // Check if sensor_index is already used
        if (sensor_indices.find(sensor_index) != sensor_indices.end())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "[{}] Duplicate sensor_index {} found in configuration",
                                  (void *)interactor, sensor_index);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        sensor_indices.insert(sensor_index);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManager::validate_multi_instance_restrictions()
{
    // Only validate if there are multiple instances
    if (m_interactor_to_hml_config.size() <= 1)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    for (const auto &[interactor, hml_config] : m_interactor_to_hml_config)
    {
        // Check if EIS is enabled
        if (hml_config.get_profile_in_use()->stabilizer_settings.eis.enabled)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "[{}] EIS is enabled in configuration but multiple instances are active. EIS "
                                  "must be disabled when using multiple instances.",
                                  (void *)interactor);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        // Check if HDR is enabled
        if (hml_config.get_profile_in_use()->iq_settings.hdr.enabled)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "[{}] HDR is enabled in configuration but multiple instances are active. HDR "
                                  "must be disabled when using multiple instances.",
                                  (void *)interactor);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        // Check if pre-ISP denoise is enabled
        if (hml_config.get_profile_in_use()->iq_settings.denoise.enabled &&
            hml_config.get_profile_in_use()->iq_settings.denoise.bayer &&
            !hml_config.get_profile_in_use()->iq_settings.denoise.bayer_network_config.dgain_channel.empty())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "[{}] Pre-ISP denoise is enabled in configuration but multiple instances are active. "
                                  "Pre-ISP denoise must be disabled when using multiple instances.",
                                  (void *)interactor);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<medialib_config_t, media_library_return> MediaLibraryConfig::get_medialib_config_from_hml_json_config(
    const std::string &medialib_json_config_string) const
{
    ConfigParser medialib_config_parser(ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG);
    medialib_config_t medialib_conf;
    auto status =
        medialib_config_parser.config_string_to_struct<medialib_config_t>(medialib_json_config_string, medialib_conf);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse medialib config json string");
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    if (std::find_if(medialib_conf.profiles.begin(), medialib_conf.profiles.end(), [&](const auto &profile) {
            return profile.name == medialib_conf.default_profile;
        }) == medialib_conf.profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Default profile '{}' not found in profiles", medialib_conf.default_profile);
        return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    return medialib_conf;
}

tl::expected<std::map<std::string, std::shared_ptr<config_profile_t>>, media_library_return> MediaLibraryConfig::
    get_profiles_by_name_from_hml_conf(const medialib_config_t &medialib_conf) const
{

    ConfigParser config_manager = ConfigParser(ConfigSchema::CONFIG_SCHEMA_PROFILE);
    std::map<std::string, std::shared_ptr<config_profile_t>> name_to_profile;
    for (const auto &profile : medialib_conf.profiles)
    {
        std::shared_ptr<config_profile_t> profile_config = std::make_shared<config_profile_t>();
        LOGGER__MODULE__INFO(MODULE_NAME, "Parsing profile: {} from file: {}", profile.name, profile.config_file);
        LOGGER__MODULE__TRACE(MODULE_NAME, "Profile config content: {}", profile.flattened_config_file_content.dump());
        auto status = config_manager.config_string_to_struct<config_profile_t>(
            profile.flattened_config_file_content.dump(), *profile_config);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse profile: {} from file: {}", profile.name,
                                  profile.config_file);
            return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }
        name_to_profile[profile.name] = profile_config;
        name_to_profile[profile.name]->name = profile.name;
    }

    return name_to_profile;
}

media_library_return MediaLibraryConfig::reset_overriden_params()
{
    auto medialib_conf_exp = get_medialib_config_from_hml_json_config(hml_json_config);
    if (!medialib_conf_exp.has_value())
    {
        return medialib_conf_exp.error();
    }
    medialib_config_t medialib_conf = medialib_conf_exp.value();
    auto name_to_profile_exp = get_profiles_by_name_from_hml_conf(medialib_conf);
    if (!name_to_profile_exp.has_value())
    {
        return name_to_profile_exp.error();
    }
    profile_by_name = std::move(name_to_profile_exp.value());
    default_profile_name = medialib_conf.default_profile;
    version = medialib_conf.version;
    set_profile_in_use(profile_in_use);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryConfig::set(const std::string &medialib_json_config_string)
{
    auto medialib_conf_exp = get_medialib_config_from_hml_json_config(medialib_json_config_string);
    if (!medialib_conf_exp.has_value())
    {
        return medialib_conf_exp.error();
    }
    medialib_config_t medialib_conf = medialib_conf_exp.value();
    auto name_to_profile_exp = get_profiles_by_name_from_hml_conf(medialib_conf);
    if (!name_to_profile_exp.has_value())
    {
        return name_to_profile_exp.error();
    }
    profile_by_name = std::move(name_to_profile_exp.value());
    default_profile_name = medialib_conf.default_profile;
    version = medialib_conf.version;
    set_profile_in_use(default_profile_name);
    hml_json_config = medialib_json_config_string;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryConfig::set_dummy_profile(const frontend_config_t &frontend_config)
{
    static constexpr const char *DUMMY_GST_PROFILE_NAME = "dummy_gst_mode_profile";

    std::map<std::string, std::shared_ptr<config_profile_t>> name_to_profile_tmp;
    std::shared_ptr<config_profile_t> profile_config = std::make_shared<config_profile_t>();
    profile_config->from_frontend_config(frontend_config);
    profile_config->name = DUMMY_GST_PROFILE_NAME;
    name_to_profile_tmp[DUMMY_GST_PROFILE_NAME] = profile_config;

    profile_by_name = std::move(name_to_profile_tmp);
    default_profile_name = DUMMY_GST_PROFILE_NAME;
    set_profile_in_use(default_profile_name);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManagerInteractor::backup_profiles(const std::string &backup_folder_path)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Backing up profiles to: {}", backup_folder_path);
    std::lock_guard<std::mutex> lock(interaction_mtx);

    auto hml_config_exp = ConfigManager::get_instance().get_medialib_config(this);
    if (!hml_config_exp.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get medialib config");
        return hml_config_exp.error();
    }
    return ConfigBackup::backup_profiles(hml_config_exp.value(), backup_folder_path);
}

media_library_return MediaLibraryConfig::set_profile_in_use(const std::string &profile_to_set_name)
{
    if (profile_by_name.find(profile_to_set_name) == profile_by_name.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Profile '{}' does not exist in medialib_config", profile_to_set_name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (profile_in_use != profile_to_set_name)
    {
        fallback_profile = profile_in_use;
    }
    profile_in_use = profile_to_set_name;
    return MEDIA_LIBRARY_SUCCESS;
}

std::shared_ptr<config_profile_t> MediaLibraryConfig::get_profile_in_use() const
{
    return profile_by_name.at(profile_in_use);
}

tl::expected<std::shared_ptr<config_profile_t>, media_library_return> MediaLibraryConfig::
    get_profile_in_use_without_overriden_params() const
{
    auto medialib_conf_exp = get_medialib_config_from_hml_json_config(hml_json_config);
    if (!medialib_conf_exp.has_value())
    {
        return tl::unexpected(medialib_conf_exp.error());
    }
    medialib_config_t medialib_conf = medialib_conf_exp.value();
    auto name_to_profile_exp = get_profiles_by_name_from_hml_conf(medialib_conf);
    if (!name_to_profile_exp.has_value())
    {
        return tl::unexpected(name_to_profile_exp.error());
    }

    return name_to_profile_exp.value().at(profile_in_use);
}

std::optional<std::shared_ptr<config_profile_t>> MediaLibraryConfig::get_fallback_profile() const
{
    if (!fallback_profile.empty())
    {
        return profile_by_name.at(fallback_profile);
    }
    return std::nullopt;
}
