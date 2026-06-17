#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace hailo_analytics::pipeline
{

/**
 * @brief Base entry stored in the PipelineDatabase.
 *
 * Derived types add domain-specific fields (e.g., OCR text, confidence).
 * The database uses `last_updated` for TTL eviction.
 */
struct PipelineDBEntry
{
    std::chrono::steady_clock::time_point last_updated;
    virtual ~PipelineDBEntry() = default;
};

/**
 * @brief Generic, thread-safe, key-value store with TTL eviction.
 *
 * Stages capture a shared_ptr<PipelineDatabase> in their callback lambdas.
 * Thread safety: shared_lock for reads, unique_lock for writes.
 */
class PipelineDatabase
{
  public:
    static constexpr std::chrono::seconds DEFAULT_TTL{30};
    static constexpr size_t DEFAULT_MAX_ENTRIES = 10000;

    PipelineDatabase(std::chrono::seconds ttl = DEFAULT_TTL, size_t max_entries = DEFAULT_MAX_ENTRIES);

    void put(int key, std::shared_ptr<PipelineDBEntry> entry);
    std::shared_ptr<PipelineDBEntry> get(int key) const;
    bool contains(int key) const;
    void remove(int key);
    void clear();
    size_t size() const;

  private:
    void evict_expired();

    mutable std::shared_mutex m_mutex;
    std::unordered_map<int, std::shared_ptr<PipelineDBEntry>> m_entries;
    std::chrono::seconds m_ttl;
    size_t m_max_entries;
};

using PipelineDatabasePtr = std::shared_ptr<PipelineDatabase>;

} // namespace hailo_analytics::pipeline
