#pragma once

// General includes
#include <string>
#include <chrono>

// Infra includes
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"

class StageTracing
{
  protected:
    std::string m_stage_name;
    uint m_counter;
    bool m_first_fps_measured;
    std::chrono::steady_clock::time_point m_last_time;

#ifdef HAVE_PERFETTO
    std::string m_trace_processing_string;
    perfetto::DynamicString m_trace_processing_name;
    std::string m_fps_counter_name;
    perfetto::NamedTrack m_stage_track;
#endif

  public:
    explicit StageTracing(const std::string &name);
    virtual ~StageTracing() = default;

    virtual void trace_fps();
    virtual void trace_processing_start();
    virtual void trace_processing_end();
    virtual void trace_async_event_begin(uint64_t unique_id);
    virtual void trace_async_event_begin(uint64_t unique_id, const char *category);
    virtual void trace_async_event_end(uint64_t unique_id);

    virtual void increment_counter();
};
