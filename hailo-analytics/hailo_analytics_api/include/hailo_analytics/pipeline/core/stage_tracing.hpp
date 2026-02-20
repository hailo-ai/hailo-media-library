#pragma once

// General includes
#include <string>
#include <chrono>
#include <memory>
#include <unordered_set>

// Infra includes
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

namespace hailo_analytics::pipeline
{

// Helper function to convert concurrent_stream_ids set to comma-separated string
std::string concurrent_streams_to_string(const std::unordered_set<std::string> &stream_ids);

class StageTracing
{
  protected:
    std::string m_stage_name;
    uint m_counter;
    bool m_first_fps_measured;
    std::chrono::steady_clock::time_point m_last_time;
    std::string m_trace_processing_string;
    std::string m_fps_counter_name;

#ifdef HAVE_PERFETTO
    perfetto::DynamicString m_trace_processing_name;
    perfetto::NamedTrack m_stage_track;
#else
    std::string m_trace_processing_name;
    std::string m_stage_track;
#endif

  public:
    explicit StageTracing(const std::string &name);
    virtual ~StageTracing() = default;

    virtual void trace_fps();

    // Trace processing start with optional debug parameters
    // Usage: trace_processing_start(buffer)  // basic usage
    //        trace_processing_start(buffer, "param_name1", value1, "param_name2", value2, ...)  // with debug params
    // Automatically includes isp_timestamp_ms and concurrent_streams when data is provided
    template <typename... Args> void trace_processing_start(BufferPtr data = nullptr, Args &&...args)
    {
        if (data && data->get_buffer())
        {
            std::string concurrent_streams = concurrent_streams_to_string(data->get_buffer()->concurrent_stream_ids);

#ifdef HAVE_PERFETTO
            HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_trace_processing_name, m_stage_track, HAILO_ANALYTICS_DETAILED_CATEGORY,
                                              "isp_timestamp_ms", data->get_buffer()->isp_timestamp_ns / 1000000,
                                              "concurrent_streams", perfetto::DynamicString(concurrent_streams),
                                              std::forward<Args>(args)...);
#else
            HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_trace_processing_name, m_stage_track, HAILO_ANALYTICS_DETAILED_CATEGORY,
                                              std::forward<Args>(args)...);
#endif
        }
        else
        {
            HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_trace_processing_name, m_stage_track, HAILO_ANALYTICS_DETAILED_CATEGORY,
                                              std::forward<Args>(args)...);
        }
    }

    virtual void trace_processing_end(BufferPtr data = nullptr);
    virtual void trace_async_event_begin(uint64_t unique_id);
    virtual void trace_async_event_begin(uint64_t unique_id, const char *category);
    virtual void trace_async_event_end(uint64_t unique_id);

    virtual void increment_counter();
};

} // namespace hailo_analytics::pipeline
