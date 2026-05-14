#pragma once

// General includes
#include <string>
#include <chrono>
#include <memory>
#include <unordered_set>

// Infra includes
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

    // Opaque wrapper for Perfetto types (perfetto::DynamicString, perfetto::NamedTrack).
    // Keeps <hailo_perfetto.h> (~300K preprocessor lines) out of this header.
    struct PerfettoImpl;
    std::unique_ptr<PerfettoImpl> m_perfetto;

  public:
    explicit StageTracing(const std::string &name);
    virtual ~StageTracing();

    virtual void trace_fps();

    // Trace processing start with optional debug parameters
    // Usage: trace_processing_start(buffer)  // basic usage
    //        trace_processing_start(buffer, "param_name1", value1, "param_name2", value2, ...)  // with debug params
    // Automatically includes isp_timestamp_ms and concurrent_streams when data is provided
    //
    // The 0-args overload is a non-template defined in stage_tracing.cpp (most common case).
    // The variadic overload is defined in stage_tracing_perfetto.hpp — include that header
    // in .cpp files that pass extra debug parameters.
    void trace_processing_start(BufferPtr data = nullptr);

    template <typename First, typename... Rest>
    void trace_processing_start(BufferPtr data, First &&first, Rest &&...rest);

    virtual void trace_processing_end(BufferPtr data = nullptr);
    virtual void trace_async_event_begin(uint64_t unique_id);
    virtual void trace_async_event_begin(uint64_t unique_id, const char *category);
    virtual void trace_async_event_end(uint64_t unique_id);

    virtual void increment_counter();
};

} // namespace hailo_analytics::pipeline
