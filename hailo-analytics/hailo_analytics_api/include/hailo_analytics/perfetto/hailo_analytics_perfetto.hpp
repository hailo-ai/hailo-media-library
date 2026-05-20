/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#ifdef HAVE_PERFETTO

#include <hailo_perfetto.h> // IWYU pragma: keep // IWYU pragma: export

#define HAILO_ANALYTICS_CATEGORY "hailo_analytics"
#define HAILO_ANALYTICS_DETAILED_CATEGORY "hailo_analytics_detailed"

HAILO_PERFETTO_DEFINE_CATEGORIES(hailo_analytics_perfetto,
                                 perfetto::Category(HAILO_ANALYTICS_CATEGORY)
                                     .SetTags("hailo")
                                     .SetDescription("Events from Hailo Analytics infrastructure"),
                                 perfetto::Category(HAILO_ANALYTICS_DETAILED_CATEGORY)
                                     .SetTags("hailo")
                                     .SetDescription("Detailed events from Hailo Analytics infrastructure"));

#define HAILO_ANALYTICS_TRACK (perfetto::NamedTrack("Hailo Analytics", 0))
#define HAILO_ANALYTICS_FRAMERATE_TRACK (perfetto::NamedTrack("Framerate", 0, HAILO_ANALYTICS_TRACK))
#define HAILO_ANALYTICS_QUEUE_LEVEL_TRACK (perfetto::NamedTrack("Queue Level", 0, HAILO_ANALYTICS_TRACK))
#define HAILO_ANALYTICS_PROCESSING_TRACK (perfetto::NamedTrack("Processing", 0, HAILO_ANALYTICS_TRACK))
#define HAILO_ANALYTICS_PROCESSING_THREADED_TRACK (perfetto::ThreadSubTrack::Current(HAILO_ANALYTICS_PROCESSING_TRACK))

#define HAILO_ANALYTICS_TRACE_EVENT_BEGIN(event_name, track, category, ...)                                            \
    TRACE_EVENT_BEGIN((category), (event_name), (track), ##__VA_ARGS__)
#define HAILO_ANALYTICS_TRACE_EVENT_END(track, category) TRACE_EVENT_END((category), (track))
#define HAILO_ANALYTICS_TRACE_EVENT(event_name, track, category, ...)                                                  \
    TRACE_EVENT((category), (event_name), (track), ##__VA_ARGS__)

/* async event API - will create a dedicated track for this async event. event_name has to match between _BEGIN and _END
 */
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track, category)                                \
    HAILO_ANALYTICS_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack((event_name), (id), (parent_track)),          \
                                      (category))
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END(event_name, id, parent_track, category)                                  \
    HAILO_ANALYTICS_TRACE_EVENT_END(perfetto::NamedTrack((event_name), (id), (parent_track)), (category))

/* async event API with custom track name - allows different event names on the same track for color variety */
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(event_name, id, track_name, parent_track, category)         \
    HAILO_ANALYTICS_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack((track_name), (id), (parent_track)),          \
                                      (category))
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END_WITH_TRACK(id, track_name, parent_track, category)                       \
    HAILO_ANALYTICS_TRACE_EVENT_END(perfetto::NamedTrack((track_name), (id), (parent_track)), (category))

#define HAILO_ANALYTICS_TRACE_CUSTOM_COUNTER(value, track, category) TRACE_COUNTER((category), (track), (value))

#define HAILO_ANALYTICS_TRACE_COUNTER(counter_name, value, parent_track, category)                                     \
    HAILO_ANALYTICS_TRACE_CUSTOM_COUNTER(                                                                              \
        (value), perfetto::CounterTrack(perfetto::DynamicString(counter_name), 0, (parent_track)), (category))

#else // no HAVE_PERFETTO

/*
assert either HAVE_PERFETTO or PERFETTO_NOT_FOUND is defined to avoid meson bugs
*/
#ifndef PERFETTO_NOT_FOUND
#error "Perfetto define not found - probably meson target is missing common_args"
#endif // no PERFETTO_NOT_FOUND

/* no perfetto - empty macros */
#define HAILO_ANALYTICS_TRACE_EVENT_BEGIN(event_name, track, category, ...)
#define HAILO_ANALYTICS_TRACE_EVENT_END(track, category)
#define HAILO_ANALYTICS_TRACE_EVENT(event_name, track, category, ...)
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track, category)
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END(event_name, id, parent_track, category)
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(event_name, id, track_name, parent_track, category)
#define HAILO_ANALYTICS_TRACE_ASYNC_EVENT_END_WITH_TRACK(id, track_name, parent_track, category)
#define HAILO_ANALYTICS_TRACE_CUSTOM_COUNTER(value, track, category)
#define HAILO_ANALYTICS_TRACE_COUNTER(counter_name, value, parent_track, category)
#endif // HAVE_PERFETTO
