#pragma once
#include "media_library_types.hpp" // IWYU pragma: export
#include "encoder.hpp"             // IWYU pragma: export
#include "frontend.hpp"            // IWYU pragma: export
#include "config_parser.hpp"       // IWYU pragma: export
#include "utils.hpp"               // IWYU pragma: export

#include <hailo/hailort.h>
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <variant>
#include <vector>

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
