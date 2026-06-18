#pragma once

#include <stddef.h>
#include <tl/expected.hpp>
#include <string>
#include <map>
#include <optional>
#include <utility>
#include <mutex>
#include <memory>

#include "media_library_types.hpp"
#include "config_parser.hpp"
#include "sensor_types.hpp"
#include "encoder_config_types.hpp"

enum class restricted_profile_type_t
{
    RESTICTED_PROFILE_NONE,
    RESTICTED_PROFILE_DENOISE,
    RESTICTED_PROFILE_STREAMING,
};

struct MediaLibraryConfig
{
  private:
    std::string profile_in_use;
    std::string fallback_profile;
    tl::expected<std::map<std::string, std::shared_ptr<config_profile_t>>, media_library_return>
    get_profiles_by_name_from_hml_conf(const medialib_config_t &medialib_conf) const;
    tl::expected<medialib_config_t, media_library_return> get_medialib_config_from_hml_json_config(
        const std::string &medialib_json_config_string) const;

  public:
    std::string default_profile_name;
    std::map<std::string, std::shared_ptr<config_profile_t>> profile_by_name;
    restricted_profile_type_t current_restriction;
    std::string hml_json_config;
    std::string version;

    MediaLibraryConfig()
        : default_profile_name(), profile_by_name(),
          current_restriction(restricted_profile_type_t::RESTICTED_PROFILE_NONE), hml_json_config()
    {
    }

    media_library_return set(const std::string &medialib_json_config_string);
    media_library_return reset_overriden_params();

    // Used to create a dummy profile for gst mode / when user doesn't use profiles
    media_library_return set_dummy_profile(const frontend_config_t &frontend_config);
    media_library_return set_profile_in_use(const std::string &profile_name);
    std::shared_ptr<config_profile_t> get_profile_in_use() const;
    tl::expected<std::shared_ptr<config_profile_t>, media_library_return> get_profile_in_use_without_overriden_params()
        const;
    std::optional<std::shared_ptr<config_profile_t>> get_fallback_profile() const;
};

class ConfigManagerInteractor;

class ConfigManager
{
  public:
    static ConfigManager &get_instance();

    bool is_dual_sensor();

    media_library_return register_interactor(ConfigManagerInteractor *interactor);
    void unregister_interactor(ConfigManagerInteractor *interactor);

    media_library_return set_medialib_config(ConfigManagerInteractor *interactor,
                                             const std::string &medialib_json_config_str, bool force = false);
    media_library_return set_medialib_config_in_gst_mode(ConfigManagerInteractor *interactor,
                                                         const MediaLibraryConfig &medialib_config);
    tl::expected<MediaLibraryConfig, media_library_return> get_medialib_config(
        const ConfigManagerInteractor *interactor);

    media_library_return switch_to_profile_by_name(const ConfigManagerInteractor *interactor,
                                                   const std::string &profile_name);
    media_library_return set_profile(const ConfigManagerInteractor *interactor,
                                     const config_profile_t &medialib_profile);

    media_library_return validate_profile_rules(ConfigManagerInteractor *interactor,
                                                const std::string &profile_json_config_str);

    void set_restricted_profile_type(const ConfigManagerInteractor *interactor,
                                     restricted_profile_type_t restricted_profile_type);

    bool has_interactors();

    media_library_return reset_profiles_overriden_params(const ConfigManagerInteractor *interactor);

    std::mutex registration_mutex;

  private:
    ConfigManager();
    ~ConfigManager();

    media_library_return validate_configuration(const std::string &config_string, ConfigSchema config_schema_type);
    media_library_return validate_metadata(const std::string &config_string);
    bool is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type);

    static constexpr size_t MAX_INSTANCES = 2;

    std::map<const ConfigManagerInteractor *, MediaLibraryConfig> m_interactor_to_hml_config;

    media_library_return validate_sensor_index_uniqueness();
    media_library_return validate_multi_instance_restrictions();
};

extern ConfigManagerInteractor *gst_mode_config_manager_interactor;

class ConfigManagerInteractor
{
  private:
    std::optional<std::string> get_connected_sensor_name() const;
    media_library_return is_sensor_connected_by_name(const std::string &sensor_type_str) const;
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address(const MediaLibraryConfig &hml_config) const;
    ConfigManagerInteractor();

    static std::mutex interaction_mtx;
    std::string m_default_medialib_config;

  public:
    static tl::expected<std::unique_ptr<ConfigManagerInteractor>, media_library_return> create(
        const std::string &medialib_json_config_string);
    static tl::expected<std::unique_ptr<ConfigManagerInteractor>, media_library_return> create_dummy_profile_interactor(
        const frontend_config_t &frontend_config);
    ~ConfigManagerInteractor();

    media_library_return reset_profiles();
    media_library_return switch_to_profile_by_name(const std::string &profile_name);
    media_library_return set_profile(const config_profile_t &profile);
    media_library_return validate_profile_rules(const std::string &profile_json_config_str);
    config_profile_t set_frontend_config(const frontend_config_t &frontend_config);
    std::optional<std::shared_ptr<const config_profile_t>> get_profile_by_name(const std::string &profile_name) const;
    std::shared_ptr<config_profile_t> get_default_profile() const;
    frontend_config_t get_frontend_config() const;
    std::string get_frontend_config_as_string() const;
    std::map<output_stream_id_t, encoder_config_t> get_encoder_configs() const;
    std::optional<SensorType> get_sensor_type() const;
    std::map<output_stream_id_t, config_encoded_output_stream_t> get_encoded_output_streams() const;
    std::optional<std::string> get_sensor_entry_config() const;
    std::string get_3a_config() const;
    bool is_dual_sensor() const;
    std::string profile_struct_to_string(const config_profile_t &profile);
    restricted_profile_type_t get_restricted_profile_type() const;
    void set_restricted_profile_type(restricted_profile_type_t restriction_type);
    std::optional<std::shared_ptr<const config_profile_t>> get_current_profile() const;
    std::optional<std::shared_ptr<const config_profile_t>> get_current_profile_without_overriden_params() const;
    std::optional<std::shared_ptr<const config_profile_t>> get_fallback_profile() const;
    tl::expected<MediaLibraryConfig, media_library_return> get_medialib_config() const;
    std::optional<std::string> get_isp_sensor_symlink_path() const;
    std::optional<std::string> get_isp_3a_config_symlink_path() const;
    media_library_return update_encoder_streams_for_rotation() const;
    media_library_return backup_profiles(const std::string &backup_filepath);
};
