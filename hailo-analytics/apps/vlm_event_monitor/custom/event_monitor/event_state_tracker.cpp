#include "event_state_tracker.hpp"

namespace vlm_event_monitor
{

EventStateTracker::EventStateTracker(std::chrono::seconds cooldown) : m_cooldown(cooldown)
{
}

bool EventStateTracker::try_emit(uint32_t event_id)
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_last_triggered.find(event_id);
    if (it != m_last_triggered.end() && (now - it->second) < m_cooldown)
    {
        return false;
    }
    m_last_triggered[event_id] = now;
    return true;
}

bool EventStateTracker::in_cooldown(uint32_t event_id) const
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_last_triggered.find(event_id);
    return it != m_last_triggered.end() && (now - it->second) < m_cooldown;
}

void EventStateTracker::reset(uint32_t event_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_last_triggered.erase(event_id);
}

void EventStateTracker::prune_to(const std::unordered_set<uint32_t> &keep)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_last_triggered.begin(); it != m_last_triggered.end();)
    {
        if (keep.find(it->first) == keep.end())
        {
            it = m_last_triggered.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

size_t EventStateTracker::active_incident_count() const
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto &entry : m_last_triggered)
    {
        if ((now - entry.second) < m_cooldown)
        {
            count++;
        }
    }
    return count;
}

void EventStateTracker::set_cooldown(std::chrono::seconds cooldown)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cooldown = cooldown;
}

std::chrono::seconds EventStateTracker::cooldown() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cooldown;
}

} // namespace vlm_event_monitor
