#pragma once

#include <cstdint>
#include <string>

namespace vlm_event_monitor
{

// User-configured event to watch for. Per locked decision 8 of the staged plan
// there is intentionally no priority field: all configured events are evaluated
// in a single VLM inference per check cycle and share one cooldown duration.
struct UserEvent
{
    uint32_t id = 0;
    std::string description;
    bool enabled = true;
};

// Event-check loop mode. Controls how frames are sampled and how prompts are
// dispatched to the VLM. Switchable at runtime via EventCheckRunner::set_mode
// (a future REST endpoint will plumb through to that).
//
// Performance: 3 frames sampled within a 5 s window (indices 0, 2, 4 at
//              1 FPS), one aggregated VLM inference per cycle that asks
//              about all enabled events at once. Same workflow Stage 2
//              shipped with — lower latency, higher throughput.
//
// Accuracy:    4 frames sampled within a 10 s window (indices 1, 3, 5, 7
//              at 1 FPS). One "describe-this-sequence" inference runs first,
//              then one inference per enabled event with prompt
//              "<event>? just answer with Yes or No". Higher latency,
//              less prompt cross-contamination between events.
enum class EventCheckMode
{
    Performance,
    Accuracy,
};

inline const char *to_string(EventCheckMode mode)
{
    switch (mode)
    {
    case EventCheckMode::Performance:
        return "performance";
    case EventCheckMode::Accuracy:
        return "accuracy";
    }
    return "performance";
}

} // namespace vlm_event_monitor
