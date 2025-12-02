#pragma once
#include "media_library/media_library_types.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/config_parser.hpp"
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

enum class media_library_pipeline_state_t
{
    PIPELINE_STATE_UNINITIALIZED,
    PIPELINE_STATE_RUNNING,
    PIPELINE_STATE_STOPPED,
};
