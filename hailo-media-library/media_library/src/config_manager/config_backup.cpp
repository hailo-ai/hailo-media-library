#include "config_backup.hpp"
#include "config_parser.hpp"
#include "config_validator.hpp"
#include "media_library_types.hpp"
#include "logger_macros.hpp"
#include "files_utils.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <climits>
#include <tl/expected.hpp>

#define MODULE_NAME LoggerType::Config
using json = nlohmann::json;

media_library_return ConfigBackup::write_config_json_to_file(nlohmann::json config_json, const std::string &dest_path)
{
    // Remove old metadata if it exists
    if (config_json.contains("metadata"))
    {
        config_json.erase("metadata");
    }

    auto metadata_result = ConfigValidator::create_backup_meta_data(config_json);
    if (!metadata_result.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create metadata for JSON file: {}", dest_path);
        return MEDIA_LIBRARY_ERROR;
    }
    config_json["metadata"] = metadata_result.value();

    std::string json_str = config_json.dump(2);
    return files_utils::write_string_to_file_atomic(dest_path, json_str);
}

template <typename ConfigType>
media_library_return ConfigBackup::backup_config_section(ConfigSchema schema, const ConfigType &config_section,
                                                         const std::string &config_path, const std::string &wrapper_key,
                                                         bool unwrap_if_wrapped)
{
    ConfigParser parser(schema);
    std::string config_str = parser.config_struct_to_string(config_section);
    nlohmann::json config_json_raw = nlohmann::json::parse(config_str);

    // If wrapper_key is provided, handle wrapping/unwrapping
    if (!wrapper_key.empty())
    {
        // If unwrap_if_wrapped is true and already wrapped, unwrap it first
        nlohmann::json config_content;
        if (unwrap_if_wrapped && config_json_raw.contains(wrapper_key) && config_json_raw[wrapper_key].is_object())
        {
            config_content = config_json_raw[wrapper_key];
        }
        else
        {
            config_content = config_json_raw;
        }

        if (config_content.is_null() || config_content.empty())
        {
            return MEDIA_LIBRARY_ERROR;
        }

        // Wrap in the specified key
        nlohmann::json wrapped_json;
        wrapped_json[wrapper_key] = config_content;
        return write_config_json_to_file(wrapped_json, config_path);
    }

    // No wrapper needed, write directly
    return write_config_json_to_file(config_json_raw, config_path);
}

tl::expected<nlohmann::json, media_library_return> ConfigBackup::backup_encoded_output_streams(
    const std::shared_ptr<config_profile_t> &profile_config, const std::string &profile_folder)
{
    nlohmann::json new_streams_array = nlohmann::json::array();
    for (const auto &[stream_id, stream_config] : profile_config->encoded_output_streams)
    {
        nlohmann::json stream_json;
        stream_json["stream_id"] = stream_id;

        // Backup encoding config
        std::string encoding_path = profile_folder + "/encoder_" + stream_id + ".json";
        media_library_return encoding_result =
            backup_config_section(ConfigSchema::CONFIG_SCHEMA_ENCODER, stream_config.encoding, encoding_path);
        if (encoding_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup encoding config for stream: {}", stream_id);
            return tl::unexpected(encoding_result);
        }
        stream_json["encoding"] = encoding_path;

        // Backup OSD config with wrapper (unwrap if already wrapped)
        std::string osd_path = profile_folder + "/osd_" + stream_id + ".json";
        media_library_return osd_result =
            backup_config_section(ConfigSchema::CONFIG_SCHEMA_OSD, stream_config.osd, osd_path, "osd", true);
        if (osd_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup OSD config for stream: {}", stream_id);
            return tl::unexpected(osd_result);
        }
        stream_json["osd"] = osd_path;

        // Backup masking config with wrapper
        std::string masking_path = profile_folder + "/masking_" + stream_id + ".json";
        media_library_return masking_result = backup_config_section(ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK,
                                                                    stream_config.masking, masking_path, "masking");
        if (masking_result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup masking config for stream: {}", stream_id);
            return tl::unexpected(masking_result);
        }
        stream_json["masking"] = masking_path;

        new_streams_array.push_back(stream_json);
    }
    return new_streams_array;
}

tl::expected<std::string, media_library_return> ConfigBackup::backup_single_profile(
    const std::string &profile_name, const std::shared_ptr<config_profile_t> &profile_config,
    const std::string &backup_folder_path)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Backing up profile: {}", profile_name);

    // Create profile subfolder
    std::string profile_folder = backup_folder_path + "/" + profile_name;
    std::error_code ec;
    std::filesystem::create_directories(profile_folder, ec);
    if (ec)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create profile folder {}: {}", profile_folder, ec.message());
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }

    // Backup individual config files
    std::string sensor_config_path = profile_folder + "/sensor_config.json";
    media_library_return result = backup_config_section(ConfigSchema::CONFIG_SCHEMA_SENSOR_CONFIG,
                                                        profile_config->sensor_config, sensor_config_path);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup sensor_config for profile: {}", profile_name);
        return tl::unexpected(result);
    }

    std::string app_settings_path = profile_folder + "/application_settings.json";
    result = backup_config_section(ConfigSchema::CONFIG_SCHEMA_APPLICATION_SETTINGS,
                                   profile_config->application_settings, app_settings_path);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup application_settings for profile: {}", profile_name);
        return tl::unexpected(result);
    }

    std::string stabilizer_path = profile_folder + "/stabilizer_settings.json";
    result = backup_config_section(ConfigSchema::CONFIG_SCHEMA_STABILIZER_SETTINGS, profile_config->stabilizer_settings,
                                   stabilizer_path);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup stabilizer_settings for profile: {}", profile_name);
        return tl::unexpected(result);
    }

    std::string iq_settings_path = profile_folder + "/iq_settings.json";
    result =
        backup_config_section(ConfigSchema::CONFIG_SCHEMA_IQ_SETTINGS, profile_config->iq_settings, iq_settings_path);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup iq_settings for profile: {}", profile_name);
        return tl::unexpected(result);
    }

    // Backup encoded output streams
    auto streams_result = backup_encoded_output_streams(profile_config, profile_folder);
    if (!streams_result.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup encoded output streams for profile: {}", profile_name);
        return tl::unexpected(streams_result.error());
    }
    nlohmann::json streams_array = streams_result.value();

    // Create and write profile JSON
    std::string profile_json_filename = profile_name + "_profile.json";
    std::string profile_json_path = profile_folder + "/" + profile_json_filename;
    nlohmann::json profile_json;
    profile_json["version"] = profile_config->version;
    profile_json["sensor_config"] = sensor_config_path;
    profile_json["application_settings"] = app_settings_path;
    profile_json["stabilizer_settings"] = stabilizer_path;
    profile_json["iq_settings"] = iq_settings_path;
    profile_json["encoded_output_streams"] = streams_array;
    result = write_config_json_to_file(profile_json, profile_json_path);
    if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write profile JSON: {}", profile_json_path);
        return tl::unexpected(result);
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Successfully backed up profile: {}", profile_name);
    return profile_json_path;
}

media_library_return ConfigBackup::backup_medialib_config(
    const MediaLibraryConfig &hml_config, const std::string &backup_folder_path,
    const std::vector<std::pair<std::string, std::string>> &backed_up_profiles)
{
    nlohmann::json medialib_json;
    medialib_json["default_profile"] = hml_config.get_profile_in_use()->name;
    medialib_json["backup_folder_path"] = backup_folder_path;

    nlohmann::json profiles_array = nlohmann::json::array();
    for (const auto &[profile_name, profile_json_path] : backed_up_profiles)
    {
        profiles_array.push_back(nlohmann::json{{"name", profile_name}, {"config_file", profile_json_path}});
    }
    medialib_json["profiles"] = profiles_array;
    medialib_json["version"] = hml_config.version;

    std::string medialib_json_path = backup_folder_path + "/medialib_config.json";
    return write_config_json_to_file(medialib_json, medialib_json_path);
}

media_library_return ConfigBackup::backup_profiles(const MediaLibraryConfig &hml_config,
                                                   const std::string &backup_folder_path)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Backing up profiles to: {}", backup_folder_path);

    if (hml_config.profile_by_name.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No profiles to backup");
        return MEDIA_LIBRARY_ERROR;
    }

    std::error_code ec;
    std::filesystem::create_directories(backup_folder_path, ec);
    if (ec)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create backup folder: {}", ec.message());
        return MEDIA_LIBRARY_ERROR;
    }

    // Structure to hold profile info for medialib config
    std::vector<std::pair<std::string, std::string>> backed_up_profiles;

    // Backup each profile
    for (const auto &[profile_name, profile_config] : hml_config.profile_by_name)
    {
        auto profile_result = backup_single_profile(profile_name, profile_config, backup_folder_path);
        if (!profile_result.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to backup profile: {}", profile_name);
            return profile_result.error();
        }
        backed_up_profiles.push_back({profile_name, profile_result.value()});
    }

    if (backup_medialib_config(hml_config, backup_folder_path, backed_up_profiles) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create medialib config");
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Successfully backed up all profiles to: {}", backup_folder_path);
    return MEDIA_LIBRARY_SUCCESS;
}
