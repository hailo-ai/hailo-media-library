#pragma once

/**
 * @file stage_tracing_perfetto.hpp
 * @brief Variadic template definition for StageTracing::trace_processing_start.
 *
 * This header includes the Perfetto SDK and defines:
 *   - StageTracing::PerfettoImpl (the PIMPL struct for Perfetto types)
 *   - The variadic template body for trace_processing_start
 *
 * Include this header ONLY in .cpp files that call trace_processing_start with
 * extra debug parameters. The common 0-args overload is a non-template function
 * defined in stage_tracing.cpp and does not require this header.
 */

#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing.hpp"

namespace hailo_analytics::pipeline
{

struct StageTracing::PerfettoImpl
{
#ifdef HAVE_PERFETTO
    perfetto::DynamicString trace_processing_name;
    perfetto::NamedTrack stage_track;

    PerfettoImpl(const std::string &processing_string)
        : trace_processing_name(perfetto::DynamicString(processing_string)),
          stage_track(
              perfetto::NamedTrack(perfetto::DynamicString(processing_string), 0, HAILO_ANALYTICS_PROCESSING_TRACK))
    {
    }
#else
    std::string trace_processing_name;
    std::string stage_track;

    PerfettoImpl(const std::string &processing_string)
        : trace_processing_name(processing_string), stage_track(processing_string)
    {
    }
#endif
};

template <typename First, typename... Rest>
void StageTracing::trace_processing_start(BufferPtr data, First &&first, Rest &&...rest)
{
    (void)first;
    ((void)rest, ...);
    if (data && data->get_buffer())
    {
        std::string concurrent_streams = concurrent_streams_to_string(data->get_buffer()->concurrent_stream_ids);

#ifdef HAVE_PERFETTO
        HAILO_ANALYTICS_TRACE_EVENT_BEGIN(
            m_perfetto->trace_processing_name, m_perfetto->stage_track, HAILO_ANALYTICS_DETAILED_CATEGORY,
            "isp_timestamp_ms", data->get_buffer()->isp_timestamp_ns / 1000000, "concurrent_streams",
            perfetto::DynamicString(concurrent_streams), std::forward<First>(first), std::forward<Rest>(rest)...);
#else
        HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_perfetto->trace_processing_name, m_perfetto->stage_track,
                                          HAILO_ANALYTICS_DETAILED_CATEGORY, std::forward<First>(first),
                                          std::forward<Rest>(rest)...);
#endif
    }
    else
    {
        HAILO_ANALYTICS_TRACE_EVENT_BEGIN(m_perfetto->trace_processing_name, m_perfetto->stage_track,
                                          HAILO_ANALYTICS_DETAILED_CATEGORY, std::forward<First>(first),
                                          std::forward<Rest>(rest)...);
    }
}

} // namespace hailo_analytics::pipeline
