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

enum class media_library_throttling_state_t
{
    THROTTLING_STATE_UNINITIALIZED = 0,
    THROTTLING_STATE_FULL_PERFORMANCE,
    THROTTLING_STATE_COOLING,
    THROTTLING_STATE_S0,
    THROTTLING_STATE_S1,
    THROTTLING_STATE_S2,
    THROTTLING_STATE_S3,
    THROTTLING_STATE_S4
};
