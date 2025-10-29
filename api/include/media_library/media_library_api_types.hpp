#pragma once
#include "media_library/media_library_types.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/utils.hpp"

#include <hailo/hailort.h>
#include <string>
#include <vector>
#include <map>
#include <variant>

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>

enum class restricted_profile_type_t
{
    RESTICTED_PROFILE_NONE,
    RESTICTED_PROFILE_DENOISE,
    RESTICTED_PROFILE_STREAMING,
};

enum class media_library_pipeline_state_t
{
    PIPELINE_STATE_UNINITIALIZED,
    PIPELINE_STATE_RUNNING,
    PIPELINE_STATE_STOPPED,
};

struct MediaLibraryConfig
{
    std::string default_profile;
    std::map<std::string, config_profile_t> profiles;

    MediaLibraryConfig() : default_profile(), profiles()
    {
    }

    MediaLibraryConfig &operator=(const medialib_config_t &medialib_conf);
};
