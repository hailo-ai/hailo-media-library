#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <tl/expected.hpp>

#include "event_config.hpp"

namespace vlm_event_monitor
{

// Single source of truth for the persisted event list and the active
// event-check mode. Owns vlm_event_monitor_events.yaml. Read at startup,
// rewritten by REST PUT /api/events. Validates the four UI invariants:
//   - 1 ≤ events.size() ≤ kMaxEvents (10)
//   - 1 ≤ enabled_count ≤ kMaxEnabledEvents (5)
//   - non-empty `description` per event
//   - unique `id` values
class EventStore
{
  public:
    static constexpr size_t kMaxEvents = 10;
    static constexpr size_t kMinEvents = 1;
    static constexpr size_t kMaxEnabledEvents = 3;
    static constexpr size_t kMinEnabledEvents = 1;

    // Snapshot of what's on disk right now. file_existed=false signals
    // "first run" to the frontend.
    struct DiskSnapshot
    {
        std::vector<UserEvent> events;
        EventCheckMode mode = EventCheckMode::Performance;
        bool file_existed = false;
    };

    // Apply hook fired AFTER a successful disk write but BEFORE replace_all
    // returns. Wired in vlm_pipeline.cpp to EventCheckRunner::set_events /
    // set_mode + an SSE monitoring_status push.
    //
    // Both the previous and new event lists are passed so the consumer can
    // detect per-event description changes. The previous
    // list is the snapshot held under the same lock that swaps in the new
    // list; subsequent calls to events() will already reflect the new state.
    using ApplyFn = std::function<void(const std::vector<UserEvent> &old_events,
                                       const std::vector<UserEvent> &new_events, EventCheckMode mode)>;

    // Never fails. If the YAML file is missing or unreadable, the store
    // initialises with an empty event list and Performance mode (and logs a
    // warning). Save from the UI is the path out — the first PUT creates
    // the file.
    static std::shared_ptr<EventStore> create(std::string yaml_path);

    EventStore(const EventStore &) = delete;
    EventStore &operator=(const EventStore &) = delete;

    // In-process snapshot under lock. Used at boot to seed the runner and
    // anywhere the active state matters (e.g. /api/monitoring/status).
    std::vector<UserEvent> events() const;
    EventCheckMode mode() const;

    // Reads the YAML file fresh from disk. Used by GET /api/events. If the
    // file doesn't exist or fails to parse, returns the sentinel
    // { events:[], mode:Performance, file_existed:false }.
    DiskSnapshot read_from_disk() const;

    // Atomic replace + persist + apply. Validates → writes YAML
    // (write-tmp + rename(2)) → updates in-memory snapshot under lock →
    // invokes the apply callback. On validation failure, returns a
    // descriptive error string suitable for surfacing in a 400 response.
    tl::expected<void, std::string> replace_all(std::vector<UserEvent> events, EventCheckMode mode);

    void set_apply_callback(ApplyFn fn);

  private:
    explicit EventStore(std::string yaml_path);

    // Load on startup; used only inside create(). Best-effort — never fails.
    void load_from_disk_initial();

    std::string m_yaml_path;
    mutable std::mutex m_mutex;
    std::vector<UserEvent> m_events;
    EventCheckMode m_mode = EventCheckMode::Performance;

    mutable std::mutex m_apply_mutex;
    ApplyFn m_apply;
};

} // namespace vlm_event_monitor
