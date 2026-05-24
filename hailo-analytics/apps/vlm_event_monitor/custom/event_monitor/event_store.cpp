#include "event_store.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_event_monitor
{

namespace
{

EventCheckMode parse_mode_string(const std::string &raw, EventCheckMode fallback)
{
    std::string lower;
    lower.reserve(raw.size());
    for (char character : raw)
    {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (lower == "accuracy")
    {
        return EventCheckMode::Accuracy;
    }
    if (lower == "performance")
    {
        return EventCheckMode::Performance;
    }
    HAILO_ANALYTICS_LOG_WARN("EventStore: unknown mode '{}'; falling back to '{}'", raw, to_string(fallback));
    return fallback;
}

// Parse one events YAML document into a DiskSnapshot. file_existed must be
// passed in (caller already knows whether it found the file).
EventStore::DiskSnapshot parse_yaml_document(const std::string &yaml_text, bool file_existed)
{
    EventStore::DiskSnapshot snapshot;
    snapshot.file_existed = file_existed;

    try
    {
        YAML::Node root = YAML::Load(yaml_text);
        if (root["mode"])
        {
            snapshot.mode = parse_mode_string(root["mode"].as<std::string>(), EventCheckMode::Performance);
        }
        const YAML::Node &events = root["events"];
        if (events && events.IsSequence())
        {
            snapshot.events.reserve(events.size());
            for (const auto &node : events)
            {
                UserEvent event;
                if (node["id"])
                {
                    event.id = node["id"].as<uint32_t>();
                }
                if (node["description"])
                {
                    event.description = node["description"].as<std::string>();
                }
                if (node["enabled"])
                {
                    event.enabled = node["enabled"].as<bool>();
                }
                snapshot.events.push_back(std::move(event));
            }
        }
    }
    catch (const YAML::Exception &e)
    {
        HAILO_ANALYTICS_LOG_WARN("EventStore: YAML parse error ({}); returning empty snapshot", e.what());
        snapshot.events.clear();
        snapshot.mode = EventCheckMode::Performance;
    }
    return snapshot;
}

// Validate the four UI invariants. Returns empty string on success, a
// human-readable reason on failure.
std::string validate_events(const std::vector<UserEvent> &events)
{
    if (events.size() < EventStore::kMinEvents)
    {
        return "min " + std::to_string(EventStore::kMinEvents) + " event required";
    }
    if (events.size() > EventStore::kMaxEvents)
    {
        return "max " + std::to_string(EventStore::kMaxEvents) + " events allowed";
    }

    size_t enabled_count = 0;
    std::set<uint32_t> seen_ids;
    for (const auto &event : events)
    {
        if (event.description.empty())
        {
            return "event id " + std::to_string(event.id) + " has empty description";
        }
        if (!seen_ids.insert(event.id).second)
        {
            return "duplicate event id " + std::to_string(event.id);
        }
        if (event.enabled)
        {
            enabled_count++;
        }
    }

    if (enabled_count < EventStore::kMinEnabledEvents)
    {
        return "min " + std::to_string(EventStore::kMinEnabledEvents) + " event must be enabled";
    }
    if (enabled_count > EventStore::kMaxEnabledEvents)
    {
        return "max " + std::to_string(EventStore::kMaxEnabledEvents) + " events may be enabled";
    }
    return {};
}

// Serialise to YAML using yaml-cpp's emitter. Comments are not preserved
// across save round-trips (yaml-cpp doesn't track them) — this file is a
// pure data store, not user-edited config, so that's acceptable.
std::string emit_yaml(const std::vector<UserEvent> &events, EventCheckMode mode)
{
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "mode" << YAML::Value << to_string(mode);
    emitter << YAML::Key << "events";
    emitter << YAML::Value << YAML::BeginSeq;
    for (const auto &event : events)
    {
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "id" << YAML::Value << event.id;
        emitter << YAML::Key << "description" << YAML::Value << event.description;
        emitter << YAML::Key << "enabled" << YAML::Value << event.enabled;
        emitter << YAML::EndMap;
    }
    emitter << YAML::EndSeq;
    emitter << YAML::EndMap;
    return emitter.c_str();
}

// Atomic write: stream → tmp → rename. On rename failure the tmp file is
// removed best-effort. Returns empty string on success, a descriptive
// error otherwise.
std::string atomic_write(const std::string &target_path, const std::string &content)
{
    const std::string tmp_path = target_path + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return "failed to open " + tmp_path + " for write: " + std::strerror(errno);
        }
        out << content;
        if (!out.good())
        {
            out.close();
            std::remove(tmp_path.c_str());
            return "failed to write " + tmp_path;
        }
    }
    if (std::rename(tmp_path.c_str(), target_path.c_str()) != 0)
    {
        const int saved_errno = errno;
        std::remove(tmp_path.c_str());
        return "rename(" + tmp_path + " → " + target_path + ") failed: " + std::strerror(saved_errno);
    }
    return {};
}

} // namespace

std::shared_ptr<EventStore> EventStore::create(std::string yaml_path)
{
    auto store = std::shared_ptr<EventStore>(new EventStore(std::move(yaml_path)));
    store->load_from_disk_initial();
    return store;
}

EventStore::EventStore(std::string yaml_path) : m_yaml_path(std::move(yaml_path))
{
}

void EventStore::load_from_disk_initial()
{
    auto snapshot = read_from_disk();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!snapshot.file_existed)
    {
        HAILO_ANALYTICS_LOG_WARN(
            "EventStore: events file not found ({}); booting with empty event list + Performance mode. "
            "Save from the UI to create it.",
            m_yaml_path);
        m_events.clear();
        m_mode = EventCheckMode::Performance;
        return;
    }
    m_events = std::move(snapshot.events);
    m_mode = snapshot.mode;
    HAILO_ANALYTICS_LOG_INFO("EventStore: loaded {} event(s) from {} (mode={})", m_events.size(), m_yaml_path,
                             to_string(m_mode));
}

std::vector<UserEvent> EventStore::events() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events;
}

EventCheckMode EventStore::mode() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mode;
}

EventStore::DiskSnapshot EventStore::read_from_disk() const
{
    std::ifstream in(m_yaml_path);
    if (!in)
    {
        return DiskSnapshot{};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parse_yaml_document(buffer.str(), /*file_existed=*/true);
}

tl::expected<void, std::string> EventStore::replace_all(std::vector<UserEvent> events, EventCheckMode mode)
{
    auto reason = validate_events(events);
    if (!reason.empty())
    {
        return tl::make_unexpected(std::move(reason));
    }

    const std::string content = emit_yaml(events, mode);
    auto write_error = atomic_write(m_yaml_path, content);
    if (!write_error.empty())
    {
        HAILO_ANALYTICS_LOG_ERROR("EventStore: replace_all disk write failed: {}", write_error);
        return tl::make_unexpected(std::move(write_error));
    }

    std::vector<UserEvent> old_events;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        old_events = m_events; // snapshot before overwrite — passed to the apply hook
                               // so it can diff descriptions and reset cooldown timers.
        m_events = events;
        m_mode = mode;
    }

    ApplyFn apply_copy;
    {
        std::lock_guard<std::mutex> lock(m_apply_mutex);
        apply_copy = m_apply;
    }
    if (apply_copy)
    {
        apply_copy(old_events, events, mode);
    }

    HAILO_ANALYTICS_LOG_INFO("EventStore: persisted {} event(s) (mode={})", events.size(), to_string(mode));
    return {};
}

void EventStore::set_apply_callback(ApplyFn fn)
{
    std::lock_guard<std::mutex> lock(m_apply_mutex);
    m_apply = std::move(fn);
}

} // namespace vlm_event_monitor
