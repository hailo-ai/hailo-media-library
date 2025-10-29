#include "analytics_db.hpp"
#include "logger_macros.hpp"
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
        m_application_analytics_config.instance_segmentation_analytics_config.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "AnalyticsDB is not initialized. Call initialize() before adding entries.");
        return MEDIA_LIBRARY_ERROR;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Adding analytics entry for ID: {} at timestamp: {}", analytics_id,
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
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Analytics entry added successfully for ID: {}.", analytics_id);
    m_cv.notify_all();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return AnalyticsDB::add_detection_entry(const std::string &analytics_id,
                                                      const DetectionAnalyticsData &data)
{
    return add_entry(m_detection_entries_db, analytics_id, data,
                     m_application_analytics_config.detection_analytics_config);
}

media_library_return AnalyticsDB::add_instance_segmentation_entry(const std::string &analytics_id,
                                                                  const InstanceSegmentationAnalyticsData &data)
{
    return add_entry(m_instance_segmentation_entries_db, analytics_id, data,
                     m_application_analytics_config.instance_segmentation_analytics_config);
}

void AnalyticsDB::clear_db()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Resetting AnalyticsDB instance.");
    m_detection_entries_db.clear();
    m_instance_segmentation_entries_db.clear();
    m_application_analytics_config.detection_analytics_config.clear();
    m_application_analytics_config.instance_segmentation_analytics_config.clear();
}

void AnalyticsDB::add_configuration(application_analytics_config_t application_analytics_config)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t new_detection_ids = 0;
    size_t updated_detection_ids = 0;
    size_t new_segmentation_ids = 0;
    size_t updated_segmentation_ids = 0;

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

    // Process instance segmentation analytics config
    for (const auto &pair : application_analytics_config.instance_segmentation_analytics_config)
    {
        const auto &analytics_id = pair.first;
        const auto &config = pair.second;

        auto it = m_application_analytics_config.instance_segmentation_analytics_config.find(analytics_id);
        if (it != m_application_analytics_config.instance_segmentation_analytics_config.end())
        {
            it->second = config;
            m_instance_segmentation_entries_db[analytics_id].clear();
            updated_segmentation_ids++;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Updated existing instance segmentation analytics ID: {}", analytics_id);
        }
        else
        {
            m_application_analytics_config.instance_segmentation_analytics_config[analytics_id] = config;
            new_segmentation_ids++;
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Added new instance segmentation analytics ID: {}", analytics_id);
        }

        // Pre-populate entries DB (for both new and updated IDs)
        m_instance_segmentation_entries_db[analytics_id] = {};
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME,
                          "AnalyticsDB configuration added: {} new detection IDs, {} updated detection IDs, "
                          "{} new segmentation IDs, {} updated segmentation IDs. "
                          "Total: {} detection IDs, {} segmentation IDs.",
                          new_detection_ids, updated_detection_ids, new_segmentation_ids, updated_segmentation_ids,
                          m_application_analytics_config.detection_analytics_config.size(),
                          m_application_analytics_config.instance_segmentation_analytics_config.size());
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
    auto lower = inner_map.lower_bound(ts - delta);
    auto upper = inner_map.lower_bound(ts);
    if (lower != upper)
    {
        auto closest = lower;
        auto prev_iter = inner_map.end();
        for (auto iter = lower; iter != upper; ++iter)
        {
            if (std::abs((iter->first - ts).count()) < std::abs((closest->first - ts).count()))
            {
                closest = iter;
            }
            if (prev_iter != inner_map.end() && iter == prev_iter)
            {
                break;
            }
            prev_iter = iter;
        }
        return closest->second;
    }
    lower = inner_map.lower_bound(ts);
    upper = inner_map.upper_bound(ts + delta);
    if (lower != upper)
    {
        auto closest = lower;
        for (auto iter = lower; iter != upper; ++iter)
        {
            if (std::abs((iter->first - ts).count()) < std::abs((closest->first - ts).count()))
            {
                closest = iter;
            }
        }
        return closest->second;
    }
    // Special case: if delta is max, search the entire map for the closest entry
    if (delta == std::chrono::milliseconds::max())
    {
        if (inner_map.empty())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "[find_within_delta] Map is empty for MAX_DELTA");
            return tl::unexpected(MEDIA_LIBRARY_ERROR);
        }
        auto closest = inner_map.begin();
        auto prev_iter = inner_map.end();
        for (auto iter = inner_map.begin(); iter != inner_map.end(); ++iter)
        {
            if (std::abs((iter->first - ts).count()) < std::abs((closest->first - ts).count()))
            {
                closest = iter;
            }
            if (prev_iter != inner_map.end() && iter == prev_iter)
            {
                break;
            }
            prev_iter = iter;
        }
        return closest->second;
    }
    return tl::unexpected(MEDIA_LIBRARY_ERROR);
}

tl::expected<DetectionAnalyticsData, media_library_return> AnalyticsDB::query_detection_entry(
    const std::string &analytics_id, const AnalyticsQueryOptions &options)
{
    return query_entry<DetectionAnalyticsData>(m_detection_entries_db, analytics_id, options);
}

tl::expected<InstanceSegmentationAnalyticsData, media_library_return> AnalyticsDB::query_instance_segmentation_entry(
    const std::string &analytics_id, const AnalyticsQueryOptions &options)
{
    return query_entry<InstanceSegmentationAnalyticsData>(m_instance_segmentation_entries_db, analytics_id, options);
}

template <typename DataT, typename MapT>
tl::expected<DataT, media_library_return> AnalyticsDB::query_entry(const MapT &db, const std::string &analytics_id,
                                                                   const AnalyticsQueryOptions &options)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "[query_entry] Waiting for analytics_id: {} with query type {} at ts: {}",
                          analytics_id, static_cast<int>(options.m_type), options.m_ts.time_since_epoch().count());
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
            LOGGER__MODULE__TRACE(MODULE_NAME, "[query_entry] QueryType {}: DB size for analytics_id {}: {}",
                                  static_cast<int>(options.m_type), analytics_id, inner_map.size());
            switch (options.m_type)
            {
            case AnalyticsQueryType::Exact:
                result = find_exact<DataT>(inner_map, options.m_ts);
                if (result.has_value())
                {
                    LOGGER__MODULE__TRACE(MODULE_NAME,
                                          "[query_entry] Exact: Found entry for analytics_id: {} at ts: {}",
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
                        MODULE_NAME,
                        "[query_entry] WithinDelta: Found entry for analytics_id: {} at ts: {} (delta: {})",
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
                    LOGGER__MODULE__TRACE(MODULE_NAME,
                                          "[query_entry] Closest: Found entry for analytics_id: {} at ts: {}",
                                          analytics_id, result->ts.time_since_epoch().count());
                    found = true;
                    break;
                }
                LOGGER__MODULE__TRACE(MODULE_NAME, "[query_entry] Closest: No entry found for analytics_id: {}",
                                      analytics_id);
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

    if (!found)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Timeout waiting for analytics entry for ID: {} at timestamp: {}",
                              analytics_id, options.m_ts.time_since_epoch().count());
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }
    return result;
}
