#pragma once

#include <string>
#include <vector>

#include "event_config.hpp"

namespace vlm_event_monitor
{

// Stateless helper that builds the *single* user prompt sent to the VLM
// in Performance mode.
//
// Keep-it-simple style mirroring the proven test console: no system role,
// all instructions folded into one user message. The instruction lead-in
// (`lead_prompt`) is supplied at runtime from YAML so it can be tuned
// without recompiling; the runtime appends the numbered event list
// "1.[event1] 2.[event2] …" to the end.
//
// Per locked decision 8 of the staged plan there is no per-event priority,
// so events are emitted in their configured order with no priority tag.
// The model's expected response is "1. Yes\n2. No\n3. Yes\n…" — parsed by
// EventCheckRunner.
class PromptBuilder
{
  public:
    // Build the (single) user prompt from `lead_prompt` (the instruction
    // lead-in supplied via YAML, event_check.performance.lead_prompt)
    // followed by the numbered enabled-event list. Disabled events are
    // skipped.
    static std::string build_user_prompt(const std::string &lead_prompt, const std::vector<UserEvent> &events);
};

} // namespace vlm_event_monitor
