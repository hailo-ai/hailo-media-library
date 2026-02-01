#pragma once

// General includes
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

// Using declarations for pipeline types
using hailo_analytics::pipeline::BufferPtr;

#define CACHE_QUEUE_SIZE_DEFAULT 5
#define CACHE_SIZE_DEFAULT 30

struct DataItem
{
    uint64_t timestamp;
    bool already_sent;
    BufferPtr data;
};

class TimeSeriesCache
{
  public:
    explicit TimeSeriesCache(size_t maxSize) : m_maxSize(maxSize)
    {
    }

    // Insert new item (assume newer timestamp)
    void insert(uint64_t timestamp, bool flag, BufferPtr data)
    {
        if (m_data.size() >= m_maxSize)
        {
            m_data.pop_front(); // Remove oldest
        }
        m_data.push_back({timestamp, flag, data});
    }

    // Search by exact timestamp
    DataItem *find(uint64_t timestamp)
    {
        auto it = std::find_if(m_data.begin(), m_data.end(),
                               [timestamp](const DataItem &item) { return item.timestamp == timestamp; });
        return (it != m_data.end()) ? &(*it) : nullptr;
    }

    // Remove items older than timestamp
    void remove_older_than(uint64_t timestamp)
    {
        while (!m_data.empty() && m_data.front().timestamp < timestamp)
        {
            m_data.pop_front();
        }
    }

    // Check if all item in cache is older than timestamp
    bool is_older_than(uint64_t timestamp)
    {
        auto it = std::find_if(m_data.begin(), m_data.end(),
                               [timestamp](const DataItem &item) { return item.timestamp >= timestamp; });
        return (it != m_data.end()) ? false : true;
    }

    // Optional: get current size
    size_t size() const
    {
        return m_data.size();
    }

  private:
    std::deque<DataItem> m_data;
    size_t m_maxSize;
};

class CacheStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    size_t m_cache_size;
    TimeSeriesCache m_cache;

  public:
    inline CacheStage(std::string name, size_t cache_size = CACHE_SIZE_DEFAULT,
                      size_t queue_size = CACHE_QUEUE_SIZE_DEFAULT, bool leaky = false,
                      bool trace_processing_operations = true)
        : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
          m_cache_size(cache_size), m_cache(cache_size)
    {
    }

    inline hailo_analytics::pipeline::AppStatus init() override
    {
        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    inline hailo_analytics::pipeline::AppStatus deinit() override
    {
        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    inline void loop() override
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
                    // The item we are looking for in cache could be one frame ahead
                    // therefore we check if the item timestamp is ahead from the cache
                    // if so, we simply do nothing and continue when next frame arrives
                    // otherwise something is wrong and we show warning message and pops the item
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
};

class CacheStageBuild : public CacheStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_cache_size = CACHE_SIZE_DEFAULT;
        size_t m_queue_size = CACHE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_queue_size(size_t size)
        {
            m_queue_size = size;
            return *this;
        }
        Builder &set_cache_size(size_t size)
        {
            m_cache_size = size;
            return *this;
        }
        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }
        Builder &set_trace_opt(bool activate)
        {
            m_trace = activate;
            return *this;
        }

        std::shared_ptr<CacheStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<CacheStage>(m_stage_name.value(), m_cache_size, m_queue_size, m_leaky, m_trace);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
