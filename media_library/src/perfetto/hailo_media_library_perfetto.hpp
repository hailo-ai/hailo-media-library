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

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(MEDIA_LIBRARY_CATEGORY).SetTags("hailo").SetDescription("Events from media_library sub system"));

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

/* We want all media-library tracks to go into specific tracks inside MEDIA_LIBRARY_TRACK */
/* We specifically don't use TRACE_EVENT() directly,
   since internally, it does not forward the track_uuid into the end slice,
   which makes the trace_processor unable to find the end of events.
   This is confusing since the documentation states that custom tracks are supported by TRACE_EVENT().
*/

#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(event_name, track)                                                       \
    TRACE_EVENT_BEGIN(MEDIA_LIBRARY_CATEGORY, (event_name), (track))
#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(track) TRACE_EVENT_END(MEDIA_LIBRARY_CATEGORY, (track))

/* async event API - will create a dedicated track for this async event. event_name has to match between _BEGIN and _END
 */
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track)                                      \
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN((event_name), perfetto::NamedTrack((event_name), (id), (parent_track)))
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_END(event_name, id, parent_track)                                        \
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(perfetto::NamedTrack((event_name), (id), (parent_track)))

#define HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER(value, track) TRACE_COUNTER(MEDIA_LIBRARY_CATEGORY, (track), (value))

#define HAILO_MEDIA_LIBRARY_TRACE_COUNTER(counter_name, value, parent_track)                                           \
    HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER((value), perfetto::CounterTrack((counter_name), 0, (parent_track)))

#else // no HAVE_PERFETTO

/* assert either HAVE_PERFETTO or PERFETTO_NOT_FOUND is defined to avoid meson bugs */
#ifndef PERFETTO_NOT_FOUND
#error "Perfetto define not found - probably meson target is missing common_args"
#endif // no PERFETTO_NOT_FOUND

/* no perfetto - empty macros */
#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(event_name, track)
#define HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(track)
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_BEGIN(event_name, id, parent_track)
#define HAILO_MEDIA_LIBRARY_TRACE_ASYNC_EVENT_END(event_name, id, parent_track)
#define HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER(value, track)
#define HAILO_MEDIA_LIBRARY_TRACE_COUNTER(counter_name, value, parent_track)

#endif // HAVE_PERFETTO

#endif // HAILO_MEDIA_LIBRARY_PERFETTO_H
