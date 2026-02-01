/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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
/**
 * @file hailo_media_library_perfetto.hpp
 * @brief Perfetto utils for media library
 **/

#ifndef HAILO_MEDIA_LIBRARY_PERFETTO_H
#define HAILO_MEDIA_LIBRARY_PERFETTO_H

/* we want to support being included from both medialib core and the rest of media-lib */
#if __has_include("media_library/common.hpp")
#include "media_library/common.hpp"
#else
#include "common.hpp"
#endif

#ifdef HAVE_PERFETTO

#include <hailo_perfetto.h>

#define MEDIA_LIBRARY_CATEGORY "media_library"
#define MEDIA_LIBRARY_DETAILED_CATEGORY "media_library_detailed"

HAILO_PERFETTO_DEFINE_CATEGORIES(
    media_library_perfetto,
    perfetto::Category(MEDIA_LIBRARY_CATEGORY).SetTags("hailo").SetDescription("Events from media_library sub system"),
    perfetto::Category(MEDIA_LIBRARY_DETAILED_CATEGORY)
        .SetTags("hailo")
        .SetDescription("Detailed events from media_library sub system"));

/* all tracks that are used as parent tracks have to be registered
   in perfetto.cpp using InitCustomTrack(). */
#define MEDIA_LIBRARY_TRACK (perfetto::NamedTrack("MediaLibrary", 0))
#define BUFFER_POOLS_TRACK (perfetto::NamedTrack("Buffer Pools", 0, MEDIA_LIBRARY_TRACK))
#define DENOISE_TRACK (perfetto::NamedTrack("Denoise", 0, MEDIA_LIBRARY_TRACK))
#define HDR_TRACK (perfetto::NamedTrack("HDR", 0, MEDIA_LIBRARY_TRACK))
#define HDR_THREADED_TRACK (perfetto::ThreadSubTrack::Current(HDR_TRACK))
#define VIDEO_DEV_TRACK (perfetto::NamedTrack("Video Devices", 0, MEDIA_LIBRARY_TRACK))
#define VIDEO_DEV_THREADED_TRACK (perfetto::ThreadSubTrack::Current(VIDEO_DEV_TRACK))
#define DSP_OPS_TRACK (perfetto::NamedTrack("Dsp Operations", 0, MEDIA_LIBRARY_TRACK))
#define DSP_THREADED_TRACK (perfetto::ThreadSubTrack::Current(DSP_OPS_TRACK))
#define FPS_TRACK (perfetto::NamedTrack("Framerate", 0, MEDIA_LIBRARY_TRACK))

#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(event_name, track, category)                                             \
    TRACE_EVENT_BEGIN((category), (event_name), (track))
#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(track, category) TRACE_EVENT_END((category), (track))

/* async event API - will create a dedicated track for this async event. event_name has to match between _BEGIN and _END
 */
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track, category)                            \
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack((event_name), (id), (parent_track)),      \
                                          (category))
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_END(event_name, id, parent_track, category)                              \
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(perfetto::NamedTrack((event_name), (id), (parent_track)), (category))

#define HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER(value, track, category) TRACE_COUNTER((category), (track), (value))

#define HAILO_MEDIA_LIBRARY_TRACE_COUNTER(counter_name, value, parent_track, category)                                 \
    HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER((value), perfetto::CounterTrack((counter_name), 0, (parent_track)),       \
                                             (category))

#else // no HAVE_PERFETTO

/* assert either HAVE_PERFETTO or PERFETTO_NOT_FOUND is defined to avoid meson bugs */
#ifndef PERFETTO_NOT_FOUND
#error "Perfetto define not found - probably meson target is missing common_args"
#endif // no PERFETTO_NOT_FOUND

/* no perfetto - empty macros */
#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(event_name, track, category)
#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(track, category)
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track, category)
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_END(event_name, id, parent_track, category)
#define HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER(value, track, category)
#define HAILO_MEDIA_LIBRARY_TRACE_COUNTER(counter_name, value, parent_track, category)

#endif // HAVE_PERFETTO

#endif // HAILO_MEDIA_LIBRARY_PERFETTO_H
