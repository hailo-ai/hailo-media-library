#include "analytics_db.hpp"

#include <bits/std_abs.h>
#include <stddef.h>
#include <tl/expected.hpp>
#include <compare>
#include <iterator>
#include <ratio>
#include <unordered_map>
#include <utility>

#include "media_library_logger.hpp"

#define MODULE_NAME LoggerType::AnalyticsDB
AnalyticsDB::AnalyticsDB()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "AnalyticsDB constructor called.");
}

AnalyticsDB &AnalyticsDB::instance()
{
    static AnalyticsDB instance;
    return instance;
}

template <typename DataT, typename MapT, typename ConfigMapT>
media_library_return AnalyticsDB::add_entry(MapT &db, const std::string &analytics_id, DataT data,
                                            const ConfigMapT &config_map)
{
    if (m_application_analytics_config.detection_analytics_config.empty() &&
        m_application_analytics_config.semantic_segmentation_analytics_config.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "AnalyticsDB is not initialized. Call initialize() before adding entries.");
        return MEDIA_LIBRARY_ERROR;
    }
    LOGGER__MODULE__TRACE(MODULE_NAME, "Adding analytics entry for ID: {} at timestamp: {}", analytics_id,
                          data.ts.time_since_epoch().count());
    std::lock_guard<std::mutex> lock(m_mutex);
    auto db_it = db.find(analytics_id);
    if (db_it == db.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Analytics ID not found in DB: {}", analytics_id);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }
    auto &entries = db_it->second;
    entries[data.ts] = std::move(data);
    while (entries.size() > config_map.at(analytics_id).max_entries)
    {
        entries.erase(entries.begin());
    }
    LOGGER__MODULE__TRACE(MODULE_NAME, "Analytics entry added successfully for ID: {}.", analytics_id);
    m_cv.notify_all();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return AnalyticsDB::add_detection_entry(const std::string &analytics_id,
                                                      const DetectionAnalyticsData &data)
{
    return add_entry(m_detection_entries_db, analytics_id, data,
                     m_application_analytics_config.detection_analytics_config);
}

media_library_return AnalyticsDB::add_semantic_segmentation_entry(const std::string &analytics_id,
                                                                  const SemanticSegmentationAnalyticsData &data)
{
    return add_entry(m_semantic_segmentation_entries_db, analytics_id, data,
                     m_application_analytics_config.semantic_segmentation_analytics_config);
}

void AnalyticsDB::clear_db()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Clearing AnalyticsDB entries (preserving configuration).");
    for (auto &[id, entries] : m_detection_entries_db)
        entries.clear();
    for (auto &[id, entries] : m_semantic_segmentation_entries_db)
        entries.clear();
}

void AnalyticsDB::reset_db()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Resetting AnalyticsDB (clearing entries and configuration).");
    m_detection_entries_db.clear();
    m_semantic_segmentation_entries_db.clear();
    m_application_analytics_config = {};
}

void AnalyticsDB::add_configuration(application_analytics_config_t application_analytics_config)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t new_detection_ids = 0;
    size_t updated_detection_ids = 0;
    size_t new_semantic_segmentation_ids = 0;
    size_t updated_semantic_segmentation_ids = 0;

    // Process detection analytics config
    for (const auto &pair : application_analytics_config.detection_analytics_config)
    {
        const auto &analytics_id = pair.first;
        const auto &config = pair.second;

        auto it = m_application_analytics_config.detection_analytics_config.find(analytics_id);
        if (it != m_application_analytics_config.detection_analytics_config.end())
        {
            it->second = config;
            m_detection_entries_db[analytics_id].clear();
            updated_detection_ids++;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Updated existing detection analytics ID: {}", analytics_id);
        }
        else
        {
            m_application_analytics_config.detection_analytics_config[analytics_id] = config;
            new_detection_ids++;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Added new detection analytics ID: {}", analytics_id);
        }

        // Pre-populate entries DB (for both new and updated IDs)
        m_detection_entries_db[analytics_id] = {};
    }

    // Process semantic segmentation analytics config
    for (const auto &pair : application_analytics_config.semantic_segmentation_analytics_config)
    {
        const auto &analytics_id = pair.first;
        const auto &config = pair.second;

        auto it = m_application_analytics_config.semantic_segmentation_analytics_config.find(analytics_id);
        if (it != m_application_analytics_config.semantic_segmentation_analytics_config.end())
        {
            it->second = config;
            m_semantic_segmentation_entries_db[analytics_id].clear();
            updated_semantic_segmentation_ids++;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Updated existing semantic segmentation analytics ID: {}", analytics_id);
        }
        else
        {
            m_application_analytics_config.semantic_segmentation_analytics_config[analytics_id] = config;
            new_semantic_segmentation_ids++;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Added new semantic segmentation analytics ID: {}", analytics_id);
        }

        // Pre-populate entries DB (for both new and updated IDs)
        m_semantic_segmentation_entries_db[analytics_id] = {};
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME,
                          "AnalyticsDB configuration added: {} new detection IDs, {} updated detection IDs, "
                          "{} new semantic segmentation IDs, {} updated semantic segmentation IDs. "
                          "Total: {} detection IDs, {} semantic segmentation IDs.",
                          new_detection_ids, updated_detection_ids, new_semantic_segmentation_ids,
                          updated_semantic_segmentation_ids,
                          m_application_analytics_config.detection_analytics_config.size(),
                          m_application_analytics_config.semantic_segmentation_analytics_config.size());
}

application_analytics_config_t AnalyticsDB::get_application_analytics_config()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_application_analytics_config;
}

template <typename DataT, typename InnerMapT>
tl::expected<DataT, media_library_return> AnalyticsDB::find_closest(const InnerMapT &inner_map, Timestamp ts)
{
    if (inner_map.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Requested timestamp {} but no entries found in the map.",
                              ts.time_since_epoch().count());
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }
    auto lb = inner_map.upper_bound(ts);
    if (lb == inner_map.begin())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Requested timestamp is earlier than all entries.");
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }
    // Always return the entry with timestamp <= ts (equal or earlier)
    return std::prev(lb)->second;
}

template <typename DataT, typename InnerMapT>
tl::expected<DataT, media_library_return> AnalyticsDB::find_exact(const InnerMapT &inner_map, Timestamp ts)
{
    auto it = inner_map.find(ts);
    if (it != inner_map.end())
    {
        return it->second;
    }
    return tl::unexpected(MEDIA_LIBRARY_ERROR);
}

template <typename DataT, typename InnerMapT>
tl::expected<DataT, media_library_return> AnalyticsDB::find_within_delta(const InnerMapT &inner_map, Timestamp ts,
                                                                         std::chrono::milliseconds delta)
{
    // Special case: if delta is max, search the entire map for the closest entry
    if (delta == std::chrono::milliseconds::max())
    {
        if (inner_map.empty())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "[find_within_delta] Map is empty for MAX_DELTA");
            return tl::unexpected(MEDIA_LIBRARY_ERROR);
        }
        auto closest = inner_map.begin();
        for (auto iter = inner_map.begin(); iter != inner_map.end(); ++iter)
        {
            if (std::abs((iter->first - ts).count()) < std::abs((closest->first - ts).count()))
            {
                closest = iter;
            }
        }
        return closest->second;
    }

    // First check for exact match - this is the most common case and should be fastest
    auto exact = inner_map.find(ts);
    if (exact != inner_map.end())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "[find_within_delta] Found EXACT match at ts={}",
                              ts.time_since_epoch().count());
        return exact->second;
    }

    // Search in both directions [ts-delta, ts+delta] and find the closest entry
    auto lower = inner_map.lower_bound(ts - delta);
    auto upper = inner_map.upper_bound(ts + delta);

    if (lower == upper)
    {
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }

    auto closest = lower;
    auto min_delta = std::abs((closest->first - ts).count());

    for (auto iter = lower; iter != upper; ++iter)
    {
        auto current_delta = std::abs((iter->first - ts).count());
        if (current_delta < min_delta)
        {
            min_delta = current_delta;
            closest = iter;
        }
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "[find_within_delta] Found closest match: ts={}, delta={} ns",
                          closest->first.time_since_epoch().count(), (closest->first - ts).count());
    return closest->second;
}

tl::expected<DetectionAnalyticsData, media_library_return> AnalyticsDB::query_detection_entry(
    const std::string &analytics_id, const AnalyticsQueryOptions &options)
{
    return query_entry<DetectionAnalyticsData>(m_detection_entries_db, analytics_id, options);
}

tl::expected<SemanticSegmentationAnalyticsData, media_library_return> AnalyticsDB::query_semantic_segmentation_entry(
    const std::string &analytics_id, const AnalyticsQueryOptions &options)
{
    return query_entry<SemanticSegmentationAnalyticsData>(m_semantic_segmentation_entries_db, analytics_id, options);
}

template <typename DataT, typename MapT>
tl::expected<DataT, media_library_return> AnalyticsDB::query_entry(const MapT &db, const std::string &analytics_id,
                                                                   const AnalyticsQueryOptions &options)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Waiting for analytics_id: {} with query type {} at ts: {}", analytics_id,
                          static_cast<int>(options.m_type), options.m_ts.time_since_epoch().count());
    auto query_start_time = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(m_mutex);
    auto deadline = std::chrono::steady_clock::now() + options.m_timeout;
    bool found = false;
    tl::expected<DataT, media_library_return> result = tl::unexpected(MEDIA_LIBRARY_ERROR);
    do
    {
        auto it = db.find(analytics_id);
        if (it != db.end())
        {
            const auto &inner_map = it->second;
            LOGGER__MODULE__TRACE(MODULE_NAME, "QueryType {}: DB size for analytics_id {}: {}",
                                  static_cast<int>(options.m_type), analytics_id, inner_map.size());
            switch (options.m_type)
            {
            case AnalyticsQueryType::Exact:
                result = find_exact<DataT>(inner_map, options.m_ts);
                if (result.has_value())
                {
                    LOGGER__MODULE__TRACE(MODULE_NAME, "Exact: Found entry for analytics_id: {} at ts: {}",
                                          analytics_id, options.m_ts.time_since_epoch().count());
                    found = true;
                    break;
                }
                break;
            case AnalyticsQueryType::WithinDelta:
                result = find_within_delta<DataT>(inner_map, options.m_ts, options.m_delta);
                if (result.has_value())
                {
                    LOGGER__MODULE__TRACE(
                        MODULE_NAME, "WithinDelta: Found entry for analytics_id: {} at ts: {} (delta: {})",
                        analytics_id, options.m_ts.time_since_epoch().count(), options.m_delta.count());
                    found = true;
                    break;
                }
                break;
            case AnalyticsQueryType::Closest: {
                constexpr auto MAX_DELTA = std::chrono::milliseconds::max();
                result = find_within_delta<DataT>(inner_map, options.m_ts, MAX_DELTA);
                if (result.has_value())
                {
                    LOGGER__MODULE__TRACE(MODULE_NAME, "Closest: Found entry for analytics_id: {} at ts: {}",
                                          analytics_id, result->ts.time_since_epoch().count());
                    found = true;
                    break;
                }
                LOGGER__MODULE__TRACE(MODULE_NAME, "Closest: No entry found for analytics_id: {}", analytics_id);
                break;
            }
            default:
                LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported query type: {}", static_cast<int>(options.m_type));
                return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
                break;
            }
            if (found)
            {
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            break;
        }
    } while (m_cv.wait_until(lock, deadline) != std::cv_status::timeout);

    auto query_end_time = std::chrono::steady_clock::now();
    auto wait_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(query_end_time - query_start_time);

    if (!found)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Timeout waiting for analytics entry for ID: {} at timestamp: {} (waited {} ms)",
                              analytics_id, options.m_ts.time_since_epoch().count(), wait_time_ms.count());
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Query completed for analytics_id: {} (waited {} ms)", analytics_id,
                          wait_time_ms.count());
    return result;
}
