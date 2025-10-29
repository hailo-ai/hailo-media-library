#include "media_library/media_library_api_types.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/media_library_types.hpp"

#define MODULE_NAME LoggerType::Api

MediaLibraryConfig &MediaLibraryConfig::operator=(const medialib_config_t &medialib_conf)
{
    default_profile = medialib_conf.default_profile;

    ConfigManager config_manager = ConfigManager(ConfigSchema::CONFIG_SCHEMA_PROFILE);
    for (const auto &profile : medialib_conf.profiles)
    {
        config_profile_t profile_config;
        LOGGER__MODULE__INFO(MODULE_NAME, "Parsing profile: {} from file: {}", profile.name, profile.config_file);
        LOGGER__MODULE__TRACE(MODULE_NAME, "Profile config content: {}", profile.flattened_config_file_content.dump());
        auto status = config_manager.config_string_to_struct<config_profile_t>(
            profile.flattened_config_file_content.dump(), profile_config);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse profile: {} from file: {}", profile.name,
                                  profile.config_file);
            throw std::runtime_error("Failed to parse profile");
        }
        profiles[profile.name] = profile_config;
        profiles[profile.name].name = profile.name;
    }
    if (profiles.find(default_profile) == profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Default profile '{}' not found in profiles", default_profile);
        throw std::runtime_error("Default profile not found in profiles");
    }

    return *this;
}
