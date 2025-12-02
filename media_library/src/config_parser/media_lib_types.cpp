#include "media_library_types.hpp"
#include "json_flattener.hpp"
#include "media_library_logger.hpp"

#define MODULE_NAME LoggerType::Config

media_library_return profile_t::flatten_n_validate_config()
{
    JsonParser parser;
    LOGGER__MODULE__INFO(MODULE_NAME, "Flattening and validating profile named: {}, in file: {}", name, config_file);
    auto status = parser.flatten_profile(config_file, flattened_config_file_content);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to flatten and validate profile named: {}, in file: {}", name,
                              config_file);
        return status;
    }
    return MEDIA_LIBRARY_SUCCESS;
}
