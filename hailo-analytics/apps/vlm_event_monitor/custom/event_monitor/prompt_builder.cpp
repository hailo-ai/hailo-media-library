#include "prompt_builder.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace vlm_event_monitor
{

namespace
{
// Square brackets are reserved as event delimiters in the compiled prompt
// (see format below). Strip any the caller smuggled into the description so
// they don't confuse the model. Mirrors the same scrubbing in the test
// console (apps/vlm_console/main.cpp::prompt_for_events).
std::string scrub_brackets(std::string description)
{
    description.erase(std::remove_if(description.begin(), description.end(),
                                     [](char character) { return character == '[' || character == ']'; }),
                      description.end());
    return description;
}

// Trim trailing whitespace/newlines from the YAML lead_prompt so the
// numbered event list joins cleanly with a single space (YAML "|" block
// scalars typically end in a newline).
std::string rtrim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.pop_back();
    }
    return text;
}
} // namespace

std::string PromptBuilder::build_user_prompt(const std::string &lead_prompt, const std::vector<UserEvent> &events)
{
    std::ostringstream prompt;
    prompt << rtrim(lead_prompt);

    int next_index = 1;
    for (const auto &event : events)
    {
        if (!event.enabled)
        {
            continue;
        }
        prompt << " " << next_index << ".[" << scrub_brackets(event.description) << "]";
        next_index++;
    }
    return prompt.str();
}

} // namespace vlm_event_monitor
