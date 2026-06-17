#include "vlm_app_config_parser.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_app_config
{

namespace
{

template <typename T> T as_or_default(const YAML::Node &node, const T &fallback)
{
    if (!node || !node.IsDefined() || node.IsNull())
    {
        return fallback;
    }
    try
    {
        return node.as<T>();
    }
    catch (const YAML::Exception &e)
    {
        HAILO_ANALYTICS_LOG_WARN("YAML field type mismatch ({}); using default", e.what());
        return fallback;
    }
}

void parse_server(const YAML::Node &root, ServerInfo &out)
{
    const YAML::Node &server = root["server"];
    if (!server)
    {
        return;
    }
    out.host = as_or_default<std::string>(server["host"], out.host);
    out.port = as_or_default<int>(server["port"], out.port);
}

void parse_hailort(const YAML::Node &root, HailortDeviceConfig &out)
{
    const YAML::Node &hailort = root["hailort_device"];
    if (!hailort)
    {
        return;
    }
    out.device_id = as_or_default<std::string>(hailort["device_id"], out.device_id);
}

void parse_vlm_model(const YAML::Node &root, VlmModelConfig &out)
{
    const YAML::Node &vlm = root["vlm_model"];
    if (!vlm)
    {
        return;
    }
    out.hef_path = as_or_default<std::string>(vlm["hef_path"], out.hef_path);
    out.default_max_generated_tokens =
        as_or_default<uint32_t>(vlm["default_max_generated_tokens"], out.default_max_generated_tokens);
    out.busy_wait_timeout_ms = as_or_default<uint32_t>(vlm["busy_wait_timeout_ms"], out.busy_wait_timeout_ms);
}

void parse_event_check(const YAML::Node &root, EventCheckConfig &out)
{
    const YAML::Node &ec = root["event_check"];
    if (!ec)
    {
        return;
    }

    const YAML::Node &performance = ec["performance"];
    if (performance)
    {
        out.performance.lead_prompt =
            as_or_default<std::string>(performance["lead_prompt"], out.performance.lead_prompt);
    }

    const YAML::Node &accuracy = ec["accuracy"];
    if (accuracy)
    {
        out.accuracy.lead_prompt = as_or_default<std::string>(accuracy["lead_prompt"], out.accuracy.lead_prompt);
        out.accuracy.max_tokens = as_or_default<uint32_t>(accuracy["max_tokens"], out.accuracy.max_tokens);
    }

    const YAML::Node &dbg = ec["debug_prompt_override"];
    if (dbg)
    {
        out.debug_prompt_override.enabled = as_or_default<bool>(dbg["enabled"], out.debug_prompt_override.enabled);
        out.debug_prompt_override.prompt = as_or_default<std::string>(dbg["prompt"], out.debug_prompt_override.prompt);
        out.debug_prompt_override.max_generated_tokens =
            as_or_default<uint32_t>(dbg["max_generated_tokens"], out.debug_prompt_override.max_generated_tokens);
    }

    const YAML::Node &meta = ec["debug_metadata_save"];
    if (meta)
    {
        out.debug_metadata_save.enabled = as_or_default<bool>(meta["enabled"], out.debug_metadata_save.enabled);
        out.debug_metadata_save.keep_last =
            as_or_default<uint32_t>(meta["keep_last"], out.debug_metadata_save.keep_last);
    }

    // Stage 5 — per-event cooldown.
    out.cooldown_seconds = as_or_default<uint32_t>(ec["cooldown_seconds"], out.cooldown_seconds);
}

void parse_chat(const YAML::Node &root, ChatConfig &out)
{
    const YAML::Node &chat = root["chat"];
    if (!chat)
    {
        return;
    }
    out.pause_event_check_during_chat =
        as_or_default<bool>(chat["pause_event_check_during_chat"], out.pause_event_check_during_chat);
    out.session_timeout_seconds = as_or_default<uint32_t>(chat["session_timeout_seconds"], out.session_timeout_seconds);
    out.default_max_generated_tokens =
        as_or_default<uint32_t>(chat["default_max_generated_tokens"], out.default_max_generated_tokens);
}

} // namespace

bool VlmAppConfigParser::parse_from_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to open VLM app config: {}", path);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_from_string(buffer.str());
}

bool VlmAppConfigParser::parse_from_string(const std::string &yaml_content)
{
    try
    {
        YAML::Node root = YAML::Load(yaml_content);
        parse_server(root, m_config.server_info);
        parse_hailort(root, m_config.hailort_device_config);
        parse_vlm_model(root, m_config.vlm_model);
        parse_event_check(root, m_config.event_check);
        parse_chat(root, m_config.chat);
        m_config.events_file_path = as_or_default<std::string>(root["events_file"], m_config.events_file_path);
        return true;
    }
    catch (const YAML::Exception &e)
    {
        HAILO_ANALYTICS_LOG_ERROR("YAML parse error: {}", e.what());
        return false;
    }
}

} // namespace vlm_app_config
