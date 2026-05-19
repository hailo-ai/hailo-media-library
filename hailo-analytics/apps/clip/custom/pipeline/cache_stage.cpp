#include "cache_stage.hpp"

#include <media_library/buffer_pool.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"

TimeSeriesCache::TimeSeriesCache(size_t maxSize) : m_maxSize(maxSize)
{
}

void TimeSeriesCache::insert(uint64_t timestamp, bool flag, BufferPtr data)
{
    if (m_data.size() >= m_maxSize)
    {
        m_data.pop_front(); // Remove oldest
    }
    m_data.push_back({timestamp, flag, data});
}

DataItem *TimeSeriesCache::find(uint64_t timestamp)
{
    auto it = std::find_if(m_data.begin(), m_data.end(),
                           [timestamp](const DataItem &item) { return item.timestamp == timestamp; });
    return (it != m_data.end()) ? &(*it) : nullptr;
}

void TimeSeriesCache::remove_older_than(uint64_t timestamp)
{
    while (!m_data.empty() && m_data.front().timestamp < timestamp)
    {
        m_data.pop_front();
    }
}

bool TimeSeriesCache::is_older_than(uint64_t timestamp)
{
    auto it = std::find_if(m_data.begin(), m_data.end(),
                           [timestamp](const DataItem &item) { return item.timestamp >= timestamp; });
    return (it != m_data.end()) ? false : true;
}

size_t TimeSeriesCache::size() const
{
    return m_data.size();
}

CacheStage::CacheStage(std::string name, size_t cache_size, size_t queue_size, bool leaky,
                       bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_cache_size(cache_size), m_cache(cache_size)
{
}

hailo_analytics::pipeline::AppStatus CacheStage::init()
{
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus CacheStage::deinit()
{
    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

void CacheStage::loop()
{
    while (!m_end_of_stream)
    {
        // the first queue is the one that is condisidered the "main stream"
        BufferPtr main_buffer = m_queues[0]->pop();
        if (main_buffer == nullptr && m_end_of_stream)
        {
            break;
        }

        m_cache.insert(main_buffer->get_buffer()->isp_timestamp_ns, false, main_buffer);

        uint64_t lookup_timestamp = m_queues[1]->check_timestamp(std::chrono::milliseconds(5));

        if (lookup_timestamp)
        {
            DataItem *item = m_cache.find(lookup_timestamp);

            if (item)
            {
                if (!item->already_sent)
                {
                    send_to_subscribers(item->data);

                    item->already_sent = true;

                    m_cache.remove_older_than(lookup_timestamp);
                }
                m_queues[1]->pop();
            }
            else
            {
                if (!m_cache.is_older_than(lookup_timestamp))
                {
                    std::cout << "WARNING: cache lookup timestamp unable to find cache item!" << std::endl;
                    HAILO_ANALYTICS_LOG_WARN("cache lookup timestamp unable to find cache item!");
                    m_queues[1]->pop();
                }
            }
        }
    }
}

CacheStageBuild::Builder &CacheStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

CacheStageBuild::Builder &CacheStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

CacheStageBuild::Builder &CacheStageBuild::Builder::set_cache_size(size_t size)
{
    m_cache_size = size;
    return *this;
}

CacheStageBuild::Builder &CacheStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

CacheStageBuild::Builder &CacheStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

CacheStageBuild::Builder CacheStageBuild::create()
{
    return Builder();
}

std::shared_ptr<CacheStage> CacheStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<CacheStage>(m_stage_name.value(), m_cache_size, m_queue_size, m_leaky, m_trace);
}
