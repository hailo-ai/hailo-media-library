#include "media_library_types.hpp"
#include "json_flatner.hpp"
#include <iostream>

#define MODULE_NAME LoggerType::Config

media_library_return profile_t::flatten_n_validate_config()
{
    JsonParser parser;
    return parser.flatten_profile(config_file, flattened_config_file_content);
}
