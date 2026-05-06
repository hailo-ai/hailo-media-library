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
    explicit TimeSeriesCache(size_t maxSize);

    void insert(uint64_t timestamp, bool flag, BufferPtr data);
    DataItem *find(uint64_t timestamp);
    void remove_older_than(uint64_t timestamp);
    bool is_older_than(uint64_t timestamp);
    size_t size() const;

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
    CacheStage(std::string name, size_t cache_size = CACHE_SIZE_DEFAULT, size_t queue_size = CACHE_QUEUE_SIZE_DEFAULT,
               bool leaky = false, bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;
    hailo_analytics::pipeline::AppStatus deinit() override;
    void loop() override;
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
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size(size_t size);
        Builder &set_cache_size(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<CacheStage> buildptr() const;
    };

    static Builder create();
};
