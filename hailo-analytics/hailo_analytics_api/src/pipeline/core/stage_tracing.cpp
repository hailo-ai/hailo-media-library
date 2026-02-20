#include "hailo_analytics/pipeline/core/stage_tracing.hpp"
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"
#include <string>
#include <unordered_set>

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
      m_fps_counter_name("fps_" + name),
#ifdef HAVE_PERFETTO
      m_trace_processing_name(perfetto::DynamicString(m_trace_processing_string)),
      m_stage_track(
          perfetto::NamedTrack(perfetto::DynamicString(m_trace_processing_string), 0, HAILO_ANALYTICS_PROCESSING_TRACK))
#else
      m_trace_processing_name("processing_" + name), m_stage_track("processing_" + name)
#endif
{
    m_last_time = std::chrono::steady_clock::now();
}

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

void StageTracing::trace_processing_end([[maybe_unused]] BufferPtr data)
{
    HAILO_ANALYTICS_TRACE_EVENT_END(m_stage_track, HAILO_ANALYTICS_DETAILED_CATEGORY);
}

void StageTracing::trace_async_event_begin(uint64_t unique_id)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN(m_trace_processing_name, unique_id, HAILO_ANALYTICS_PROCESSING_TRACK,
                                            HAILO_ANALYTICS_DETAILED_CATEGORY);
}

void StageTracing::trace_async_event_end(uint64_t unique_id)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END_WITH_TRACK(
        unique_id, m_trace_processing_name, HAILO_ANALYTICS_PROCESSING_TRACK, HAILO_ANALYTICS_DETAILED_CATEGORY);
}

void StageTracing::trace_async_event_begin(uint64_t unique_id, const char *category)
{
#ifdef HAVE_PERFETTO
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(perfetto::DynamicString(category), unique_id,
                                                       m_trace_processing_name, HAILO_ANALYTICS_PROCESSING_TRACK,
                                                       HAILO_ANALYTICS_DETAILED_CATEGORY);
#else
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(category, unique_id, m_trace_processing_name,
                                                       HAILO_ANALYTICS_PROCESSING_TRACK,
                                                       HAILO_ANALYTICS_DETAILED_CATEGORY);
#endif
}

} // namespace hailo_analytics::pipeline
