#pragma once
#include <nlohmann/json.hpp>

#include "media_library_types.hpp"

class MediaLibraryRuleChecker
{
  public:
    MediaLibraryRuleChecker()
    {
        configure_logger();
    }
    void configure_logger();
    media_library_return validate_config(const nlohmann::json &config_json);
};
