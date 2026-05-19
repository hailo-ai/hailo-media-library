#include <stdint.h>
#include <media_library/buffer_pool.hpp>
#include <string>
#include <unordered_set>
#include <chrono>
#include <memory>

#include "hailo_analytics/pipeline/core/stage_tracing_perfetto.hpp"
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing.hpp"

namespace hailo_analytics::pipeline
{

// Helper function to convert concurrent_stream_ids set to comma-separated string
std::string concurrent_streams_to_string(const std::unordered_set<std::string> &stream_ids)
{
    if (stream_ids.empty())
        return "none";

    std::string flattened_streams;
    bool first = true;
    for (const auto &id : stream_ids)
    {
        if (!first)
            flattened_streams += ",";
        flattened_streams += id;
        first = false;
    }
    return flattened_streams;
}

StageTracing::StageTracing(const std::string &name)
    : m_stage_name(name), m_counter(0), m_first_fps_measured(false), m_trace_processing_string("processing_" + name),
      m_fps_counter_name("fps_" + name), m_perfetto(std::make_unique<PerfettoImpl>(m_trace_processing_string))
{
    m_last_time = std::chrono::steady_clock::now();
}

StageTracing::~StageTracing() = default;

void StageTracing::trace_fps()
{
    HAILO_ANALYTICS_TRACE_COUNTER(m_fps_counter_name, m_counter, HAILO_ANALYTICS_FRAMERATE_TRACK,
                                  HAILO_ANALYTICS_CATEGORY);
}

void StageTracing::increment_counter()
{
    // Handle first measurement
    if (!m_first_fps_measured)
    {
        m_last_time = std::chrono::steady_clock::now();
        m_first_fps_measured = true;
    }

    m_counter++;
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_seconds = std::chrono::duration<double>(current_time - m_last_time);

    if (elapsed_seconds.count() >= 1.0)
    {
        trace_fps();
        m_counter = 0;
        m_last_time = current_time;
    }
}

void StageTracing::trace_processing_start(BufferPtr data)
{
    if (data && data->get_buffer())
    {
        [[maybe_unused]] std::string concurrent_streams =
            concurrent_streams_to_string(data->get_buffer()->concurrent_stream_ids);

#ifdef HAVE_PERFETTO
        HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_perfetto->trace_processing_name, m_perfetto->stage_track,
                                          HAILO_ANALYTICS_DETAILED_CATEGORY, "isp_timestamp_ms",
                                          data->get_buffer()->isp_timestamp_ns / 1000000, "concurrent_streams",
                                          perfetto::DynamicString(concurrent_streams));
#else
        HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_perfetto->trace_processing_name, m_perfetto->stage_track,
                                          HAILO_ANALYTICS_DETAILED_CATEGORY);
#endif
    }
    else
    {
        HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_perfetto->trace_processing_name, m_perfetto->stage_track,
                                          HAILO_ANALYTICS_DETAILED_CATEGORY);
    }
}

void StageTracing::trace_processing_end(BufferPtr /*data*/)
{
    HAILO_ANALYTICS_TRACE_EVENT_END(m_perfetto->stage_track, HAILO_ANALYTICS_DETAILED_CATEGORY);
}

void StageTracing::trace_async_event_begin([[maybe_unused]] uint64_t unique_id)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN(m_perfetto->trace_processing_name, unique_id,
                                            HAILO_ANALYTICS_PROCESSING_TRACK, HAILO_ANALYTICS_DETAILED_CATEGORY);
}

void StageTracing::trace_async_event_end([[maybe_unused]] uint64_t unique_id)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END_WITH_TRACK(unique_id, m_perfetto->trace_processing_name,
                                                     HAILO_ANALYTICS_PROCESSING_TRACK,
                                                     HAILO_ANALYTICS_DETAILED_CATEGORY);
}

void StageTracing::trace_async_event_begin([[maybe_unused]] uint64_t unique_id, [[maybe_unused]] const char *category)
{
#ifdef HAVE_PERFETTO
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(
        perfetto::DynamicString(category), unique_id, m_perfetto->trace_processing_name,
        HAILO_ANALYTICS_PROCESSING_TRACK, HAILO_ANALYTICS_DETAILED_CATEGORY);
#else
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(category, unique_id, m_perfetto->trace_processing_name,
                                                       HAILO_ANALYTICS_PROCESSING_TRACK,
                                                       HAILO_ANALYTICS_DETAILED_CATEGORY);
#endif
}

} // namespace hailo_analytics::pipeline
