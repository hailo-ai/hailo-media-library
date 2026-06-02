#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace vlm_event_monitor
{

// Per-event cooldown bookkeeping. When an event fires (try_emit returns
// true), that event_id is suppressed for `cooldown` duration before the
// next try_emit can succeed. Each event has its own independent timer:
// while event A is in cooldown, event B can still fire.
//
// State is in-memory only. On app restart every event
// is immediately fire-able.
//
// Thread-safety: every public method takes the internal mutex. N is
// bounded by EventStore::kMaxEvents (10), so lock contention is trivial.
//
// Callers:
//   - EventCheckRunner::broadcast_yes_verdicts (queue worker thread):
//       try_emit() at dispatch.
//   - VlmEventMonitorPipeline apply callback (webserver thread on
//       PUT /api/events): reset() when an event's description changed,
//       prune_to() to drop deleted event ids.
//   - IntegratedWebServer::build_monitoring_status (cpp-httplib worker
//       thread, also called from the apply callback): active_incident_count().
class EventStateTracker
{
  public:
    explicit EventStateTracker(std::chrono::seconds cooldown);

    EventStateTracker(const EventStateTracker &) = delete;
    EventStateTracker &operator=(const EventStateTracker &) = delete;

    // Test-and-set under the lock. If event_id is NOT currently within
    // its cooldown window, stamp last_triggered=now() and return true
    // (caller should emit). Otherwise return false (suppress).
    bool try_emit(uint32_t event_id);

    // True if event_id is currently within its cooldown window.
    // Read-only — does NOT update state.
    bool in_cooldown(uint32_t event_id) const;

    // Drop any stored stamp for `event_id`. Called when a description
    // change is detected by the apply callback so the new prompt's first
    // verdict isn't suppressed by a stamp from the old prompt. No-op when
    // the id has no entry.
    void reset(uint32_t event_id);

    // Remove entries whose event_id is not in `keep`. Prevents the map
    // from leaking entries for events deleted via PUT /api/events.
    void prune_to(const std::unordered_set<uint32_t> &keep);

    // Number of event_ids whose stamp is still within the cooldown
    // window. O(N) walk under the lock; N ≤ 10.
    size_t active_incident_count() const;

    // Replace the cooldown duration. Not wired to any runtime endpoint
    // in Stage 5 (the YAML value is consumed once at startup); kept for
    // symmetry / future use. Does not clear existing stamps.
    void set_cooldown(std::chrono::seconds cooldown);

    std::chrono::seconds cooldown() const;

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_last_triggered;
    std::chrono::seconds m_cooldown;
};

} // namespace vlm_event_monitor
