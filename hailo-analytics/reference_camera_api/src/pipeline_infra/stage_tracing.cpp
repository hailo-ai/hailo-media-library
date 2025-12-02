#include "hailo_analytics/pipeline_infra/stage_tracing.hpp"
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"

StageTracing::StageTracing(const std::string &name)
    : m_stage_name(name), m_counter(0), m_first_fps_measured(false)
#ifdef HAVE_PERFETTO
      ,
      m_trace_processing_string("processing_" + name),
      m_trace_processing_name(perfetto::DynamicString(m_trace_processing_string)), m_fps_counter_name("fps_" + name),
      m_stage_track(
          perfetto::NamedTrack(perfetto::DynamicString(m_trace_processing_string), 0, HAILO_ANALYTICS_PROCESSING_TRACK))
#endif
{
    m_last_time = std::chrono::steady_clock::now();
}

void StageTracing::trace_fps()
{
    HAILO_ANALYTICS_TRACE_COUNTER(m_fps_counter_name, m_counter, HAILO_ANALYTICS_FRAMERATE_TRACK);
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

void StageTracing::trace_processing_start()
{
    HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_trace_processing_name, m_stage_track);
}

void StageTracing::trace_processing_end()
{
    HAILO_ANALYTICS_TRACE_EVENT_END(m_stage_track);
}

void StageTracing::trace_async_event_begin(uint64_t unique_id)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN(m_trace_processing_name, unique_id, HAILO_ANALYTICS_PROCESSING_TRACK);
}

void StageTracing::trace_async_event_end(uint64_t unique_id)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END_WITH_TRACK(unique_id, m_trace_processing_name,
                                                     HAILO_ANALYTICS_PROCESSING_TRACK);
}

void StageTracing::trace_async_event_begin(uint64_t unique_id, const char *category)
{
    HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(perfetto::DynamicString(category), unique_id,
                                                       m_trace_processing_name, HAILO_ANALYTICS_PROCESSING_TRACK);
}
