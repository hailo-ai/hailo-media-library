#pragma once

#include <cstdint>
#include <string>

namespace vlm_app_config
{

struct ServerInfo
{
    std::string host = "0.0.0.0";
    int port = 80;
};

struct HailortDeviceConfig
{
    std::string device_id = "device0";
};

struct VlmModelConfig
{
    // Path to the Qwen-VL HEF on the device
    std::string hef_path;
    // Default max generated tokens for chat / inference
    uint32_t default_max_generated_tokens = 256;
    // Busy-wait timeout (ms) for VlmInferenceManager mutex acquire
    uint32_t busy_wait_timeout_ms = 5000;
};

// Debug-only override for the event-check loop. When enabled, the runner
// sends `prompt` verbatim to the VLM with `max_generated_tokens` tokens for
// each batch, bypassing the configured event list AND the yes/no parser.
// Use this to test prompt phrasings against the live frame stream without
// recompiling. Production deployments should leave `enabled: false`.
struct DebugPromptOverride
{
    bool enabled = false;
    std::string prompt;
    uint32_t max_generated_tokens = 100;
};

// Debug-only per-cycle metadata dump for cross-verifying VLM output against
// a native (non-VLM) network. When enabled, every event-check cycle saves
// its frames + prompts + responses + statistics + VLM config to
// /var/volatile/vlm-event-metadata/<cycle>/. The runner auto-evicts older
// cycles so only `keep_last` survive on disk. /var/volatile is tmpfs —
// intended for short debug runs ONLY.
struct DebugMetadataSave
{
    bool enabled = false;
    uint32_t keep_last = 10;
};

// Tunables exclusive to Performance mode. Sourced from
// `event_check.performance`.
struct PerformanceModeConfig
{
    // Front of the (single) user prompt sent to the VLM. At runtime the
    // app appends the numbered event list "1.[event1] 2.[event2] …" to
    // the end of this string. Each event uses the runner's
    // max_generated_tokens_per_event budget.
    std::string lead_prompt = "tell me if there is any activity of the following, "
                              "answer yes or no separately for each activity listed as follow:";
};

// Tunables exclusive to Accuracy mode. Sourced from `event_check.accuracy`.
struct AccuracyModeConfig
{
    // Description inference run first per cycle. The model's response is
    // printed but not parsed. Per-event yes/no follow-ups are text-only on
    // the same VLM session.
    std::string lead_prompt = "describe this video sequence in one sentence";
    uint32_t max_tokens = 25;
};

struct EventCheckConfig
{
    // Mode-specific tuning (lead prompts, max tokens). The active mode itself
    // (Performance or Accuracy) is NOT stored here — it lives in the events
    // YAML file (see VlmAppConfig::events_file_path) so frontend Save / Load
    // round-trips are atomic with the event list.
    PerformanceModeConfig performance;
    AccuracyModeConfig accuracy;

    DebugPromptOverride debug_prompt_override;
    DebugMetadataSave debug_metadata_save;

    // Stage 5 — per-event cooldown duration (seconds). When an event
    // fires, that specific event_id is suppressed for this many seconds
    // before it can fire again. Per-event: other events can still fire
    // while this one is in cooldown. Reset for an event when its
    // description is edited via PUT /api/events. Single uniform value
    // across events (no per-priority tiers — locked decision 8 of the
    // Stage 4 plan, no per-event priority).
    uint32_t cooldown_seconds = 60;
};

struct ChatConfig
{
    // When true (default), event-check inferences are dropped from the
    // priority queue for the duration any chat session is open. When false,
    // chat and event-check interleave per the design doc §2.3.4.
    //
    // The in-flight event-check inference (if any) is allowed to complete
    // — we do NOT abort the manager mid-generation. The first /chat/stream
    // after opening a chat therefore carries a non-zero `wait_ms`.
    bool pause_event_check_during_chat = true;

    // Idle timeout (seconds). The broker's janitor closes any session whose
    // last /chat/stream activity is older than this. On expiry the frontend
    // is notified via the `chat_session_expired` SSE event.
    uint32_t session_timeout_seconds = 300;

    // Per-stream override default for max_generated_tokens. Frontend can
    // override per request via the body field of the same name.
    uint32_t default_max_generated_tokens = 256;
};

struct VlmAppConfig
{
    ServerInfo server_info;
    HailortDeviceConfig hailort_device_config;
    VlmModelConfig vlm_model;
    EventCheckConfig event_check;
    ChatConfig chat;

    // Path to the YAML file that holds the persisted event list and the
    // active event-check mode. Read at startup, rewritten by Save in the
    // frontend, re-read by Load. Missing file → app boots with empty
    // events + Performance mode and a warning log.
    std::string events_file_path = "/home/root/apps/vlm_event_monitor/resources/configs/vlm_event_monitor_events.yaml";
};

} // namespace vlm_app_config
