#include "hailo_analytics/pipeline/core/pipeline_database.hpp"
#include <mutex>

namespace hailo_analytics::pipeline
{

PipelineDatabase::PipelineDatabase(std::chrono::seconds ttl, size_t max_entries)
    : m_ttl(ttl), m_max_entries(max_entries)
{
}

void PipelineDatabase::put(int key, std::shared_ptr<PipelineDBEntry> entry)
{
    std::unique_lock lock(m_mutex);
    if (m_entries.size() > m_max_entries)
    {
        evict_expired();
    }
    entry->last_updated = std::chrono::steady_clock::now();
    m_entries[key] = std::move(entry);
}

std::shared_ptr<PipelineDBEntry> PipelineDatabase::get(int key) const
{
    std::shared_lock lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end())
    {
        return nullptr;
    }
    auto elapsed = std::chrono::steady_clock::now() - it->second->last_updated;
    if (elapsed > m_ttl)
    {
        return nullptr;
    }
    return it->second;
}

bool PipelineDatabase::contains(int key) const
{
    return get(key) != nullptr;
}

void PipelineDatabase::remove(int key)
{
    std::unique_lock lock(m_mutex);
    m_entries.erase(key);
}

void PipelineDatabase::clear()
{
    std::unique_lock lock(m_mutex);
    m_entries.clear();
}

size_t PipelineDatabase::size() const
{
    std::shared_lock lock(m_mutex);
    return m_entries.size();
}

void PipelineDatabase::evict_expired()
{
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if ((now - it->second->last_updated) > m_ttl)
        {
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace hailo_analytics::pipeline
