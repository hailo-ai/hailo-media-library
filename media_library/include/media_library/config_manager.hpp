#pragma once

#include <string>
#include <map>
#include <optional>
#include <utility>
#include <mutex>

#include "media_library_types.hpp"
#include "config_parser.hpp"
#include "sensor_types.hpp"

enum class restricted_profile_type_t
{
    RESTICTED_PROFILE_NONE,
    RESTICTED_PROFILE_DENOISE,
    RESTICTED_PROFILE_STREAMING,
};

struct MediaLibraryConfig
{
    std::string default_profile;
    std::map<std::string, config_profile_t> profiles;

    MediaLibraryConfig() : default_profile(), profiles()
    {
    }

    media_library_return set(const medialib_config_t &medialib_conf);
};

class ConfigManagerCore
{
  public:
    ConfigManagerCore();
    ~ConfigManagerCore();
    media_library_return validate_configuration(std::string config_string, ConfigSchema config_schema_type);
    media_library_return validate_metadata(std::string config_string);
    bool is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type);
    media_library_return configure_medialib(std::string medialib_json_config_string, size_t idx);
    media_library_return set_profile(std::string profile, size_t idx);
    media_library_return set_profile(config_profile_t profile, size_t idx);
    std::optional<config_profile_t> set_frontend_config(frontend_config_t frontend_config, size_t idx);
    std::optional<config_profile_t> get_profile(const std::string &profile_name, size_t idx);
    std::optional<config_profile_t> get_default_profile(size_t idx);
    std::optional<frontend_config_t> get_frontend_config(size_t idx);
    std::optional<std::string> get_frontend_config_as_string(size_t idx);
    std::optional<std::map<output_stream_id_t, encoder_config_t>> get_encoder_configs(size_t idx);
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address(size_t idx);
    std::optional<SensorType> get_sensor_type(size_t idx);
    std::optional<std::string> get_connected_sensor_name(size_t idx);
    media_library_return is_sensor_connected_by_name(const std::string &sensor_type_str, size_t idx);
    std::optional<std::map<output_stream_id_t, config_encoded_output_stream_t>> get_encoded_output_streams(size_t idx);
    media_library_return get_sensor_entry_config(std::string &sensor_entry_string, size_t idx);
    media_library_return get_3a_config(std::string &aaa_config_string, size_t idx);
    bool is_dual_sensor();

    media_library_return configure_config_profile(const std::string &config_profile_json_string);
    media_library_return validate_config_profile(const std::string &config_profile_json_string);
    config_profile_t get_config_profile() const;
    std::string get_config_profile_as_string() const;
    std::string profile_struct_to_string(config_profile_t profile);

    media_library_return initialize_instance(size_t idx);
    void cleanup_instance(size_t idx);
    std::recursive_mutex m_maps_mutex;
    std::map<size_t, MediaLibraryConfig> m_medialib_configs;
    std::map<size_t, config_profile_t> m_current_profiles;
    std::map<size_t, restricted_profile_type_t> m_restricted_profile_types;

  private:
    static constexpr size_t MAX_INSTANCES = 2;

    ConfigParser m_medialib_config_parser;
    ConfigParser m_profile_config_parser;
    ConfigParser m_frontend_config_parser;
    ConfigParser m_encoder_config_parser;
    std::map<size_t, std::string> m_medialib_json_config_strings;
    config_profile_t m_config_profile_config;
    std::string m_profile_full_config_string;

    media_library_return validate_sensor_index_uniqueness();
    media_library_return validate_multi_instance_restrictions();
};

class ConfigManager
{
  private:
    size_t m_idx;
    ConfigManagerCore &m_medialib_config_manager_core;

  public:
    ConfigManager(size_t idx, ConfigManagerCore &medialib_config_manager_core);
    ~ConfigManager();
    media_library_return initialize();
    media_library_return validate_configuration(std::string config_string, ConfigSchema config_schema_type);
    bool is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type);
    media_library_return configure_medialib(std::string medialib_json_config_string);
    media_library_return set_profile(std::string profile);
    media_library_return set_profile(config_profile_t profile);
    config_profile_t set_frontend_config(frontend_config_t frontend_config);
    config_profile_t get_profile(const std::string &profile_name);
    config_profile_t get_default_profile();
    frontend_config_t get_frontend_config();
    std::string get_frontend_config_as_string();
    std::map<output_stream_id_t, encoder_config_t> get_encoder_configs();
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address();
    std::optional<SensorType> get_sensor_type();
    std::map<output_stream_id_t, config_encoded_output_stream_t> get_encoded_output_streams();
    media_library_return get_sensor_entry_config(std::string &sensor_entry_string);
    media_library_return get_3a_config(std::string &aaa_config_string);
    bool is_dual_sensor();

    media_library_return configure_config_profile(const std::string &config_profile_json_string);
    media_library_return validate_config_profile(const std::string &config_profile_json_string);
    config_profile_t get_config_profile() const;
    std::string get_config_profile_as_string() const;
    std::string profile_struct_to_string(config_profile_t profile);

    restricted_profile_type_t get_restricted_profile_type();
    void set_restricted_profile_type(restricted_profile_type_t type);
    config_profile_t get_current_profile();
    MediaLibraryConfig get_medialib_config();
    std::string get_isp_sensor_symlink_path();
    std::string get_isp_3a_config_symlink_path();
};
