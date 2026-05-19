#pragma once

#include "common/common.hpp"
#include <optional>

namespace webserver::pipeline
{

// Returns the pipeline we should switch to in response to a detections-requirement change,
// or std::nullopt if the current pipeline is already a fit. Only the auto-switch pair
// (Basic ↔ DetectionInternal) is driven by this; user-chosen pipelines are left alone.
inline std::optional<pipeline_t> target_pipeline_for_detections(pipeline_t current, bool requires_detections)
{
    if (requires_detections && current == pipeline_t::Basic)
    {
        return pipeline_t::DetectionInternal;
    }
    if (!requires_detections && current == pipeline_t::DetectionInternal)
    {
        return pipeline_t::Basic;
    }
    return std::nullopt;
}

// User explicitly requested `requested`. If they asked for Basic while a detections
// requirement is active, route to DetectionInternal so encoder ROIs keep working without
// exposing the internal pipeline type. Other choices pass through unchanged.
inline pipeline_t effective_user_pipeline(pipeline_t requested, bool requires_detections)
{
    if (requested == pipeline_t::Basic && requires_detections)
    {
        return pipeline_t::DetectionInternal;
    }
    return requested;
}

// Hide the internal pipeline type from clients reading /ai_pipeline.
inline pipeline_t public_pipeline_type(pipeline_t actual)
{
    return actual == pipeline_t::DetectionInternal ? pipeline_t::Basic : actual;
}

} // namespace webserver::pipeline
