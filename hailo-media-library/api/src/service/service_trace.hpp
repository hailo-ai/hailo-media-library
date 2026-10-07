/**
 * @file service_trace.hpp
 * @brief Perfetto trace-event helpers for media_library_service and its
 *        gRPC client wrapper.
 *
 * Provides scoped (RAII) helpers for tagging server-side and client-side
 * code paths in Perfetto traces, plus paired-macro helpers for use in
 * template/header code where RAII isn't ergonomic.
 *
 * Server-side spans land on SERVICE_TRACK ("gRPC Server"); client-side on
 * SERVICE_CLIENT_TRACK ("gRPC Client"). Both are NamedTracks under the
 * shared MediaLibrary parent track and are registered in
 * media_library/src/trace/perfetto.cpp.
 *
 * The macros are no-ops when neither HAVE_PERFETTO nor PERFETTO_NOT_FOUND
 * is defined (e.g. the verification build target), so this header is safe
 * to include from any translation unit without forcing the perfetto SDK
 * onto build targets that don't link against it.
 *
 * Usage (RAII scoped span — recommended):
 *
 *     void some_function()
 *     {
 *         SERVICE_TRACE_SCOPE("some_function");      // begins on entry
 *         do_work();                                  // ends on scope exit
 *
 *         {
 *             SERVICE_TRACE_SCOPE("inner_phase");     // nested OK; each
 *             do_inner();                             // call site gets a
 *         }                                           // unique variable
 *     }
 *
 * Usage (paired macros — for templates / headers that don't want a class
 * type in scope):
 *
 *     void on_event() {
 *         BROKER_TRACE_BEGIN("on_event");
 *         do_work();
 *         BROKER_TRACE_END();
 *     }
 */
#pragma once

#ifdef HAVE_PERFETTO

#include "hailo_media_library_perfetto.hpp"

namespace media_library_service
{
namespace trace
{

class ServiceTraceScope
{
  public:
    explicit ServiceTraceScope(const char *name)
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(name), SERVICE_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
    ~ServiceTraceScope()
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(SERVICE_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
};

class ServiceClientTraceScope
{
  public:
    explicit ServiceClientTraceScope(const char *name)
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(name), SERVICE_CLIENT_TRACK,
                                              MEDIA_LIBRARY_CATEGORY);
    }
    ~ServiceClientTraceScope()
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(SERVICE_CLIENT_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
};

} // namespace trace
} // namespace media_library_service

// Internal helpers: token-pasting __LINE__ requires two indirections so the
// number is expanded before concatenation.
#define HMLS_TRACE_VAR_2_(prefix, line) prefix##line
#define HMLS_TRACE_VAR_(prefix, line) HMLS_TRACE_VAR_2_(prefix, line)

// Server-side: spans on SERVICE_TRACK.
#define SERVICE_TRACE_SCOPE(name)                                                                                      \
    ::media_library_service::trace::ServiceTraceScope HMLS_TRACE_VAR_(_svc_trace_, __LINE__)(name)
#define SERVICE_TRACE_RPC(name) SERVICE_TRACE_SCOPE(name)
#define SERVICE_TRACE_BEGIN(name) HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN((name), SERVICE_TRACK, MEDIA_LIBRARY_CATEGORY)
#define SERVICE_TRACE_END() HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(SERVICE_TRACK, MEDIA_LIBRARY_CATEGORY)

// Client-side: spans on SERVICE_CLIENT_TRACK.
#define SERVICE_CLIENT_TRACE_SCOPE(name)                                                                               \
    ::media_library_service::trace::ServiceClientTraceScope HMLS_TRACE_VAR_(_svc_client_trace_, __LINE__)(name)
#define CLIENT_TRACE_RPC(name) SERVICE_CLIENT_TRACE_SCOPE(name)
#define SERVICE_CLIENT_TRACE_BEGIN(name)                                                                               \
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN((name), SERVICE_CLIENT_TRACK, MEDIA_LIBRARY_CATEGORY)
#define SERVICE_CLIENT_TRACE_END() HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(SERVICE_CLIENT_TRACK, MEDIA_LIBRARY_CATEGORY)

// Aliases for use in the gRPC stream-reactor templates (event_stream_reactors.hpp).
// Server-side broker events go on SERVICE_TRACK, client-side reader events on
// SERVICE_CLIENT_TRACK — the calling thread provides the attribution.
#define BROKER_TRACE_BEGIN(name) SERVICE_TRACE_BEGIN(name)
#define BROKER_TRACE_END() SERVICE_TRACE_END()
#define READER_TRACE_BEGIN(name) SERVICE_CLIENT_TRACE_BEGIN(name)
#define READER_TRACE_END() SERVICE_CLIENT_TRACE_END()

#else // !HAVE_PERFETTO

#define SERVICE_TRACE_SCOPE(name)
#define SERVICE_TRACE_RPC(name)
#define SERVICE_TRACE_BEGIN(name)
#define SERVICE_TRACE_END()
#define SERVICE_CLIENT_TRACE_SCOPE(name)
#define CLIENT_TRACE_RPC(name)
#define SERVICE_CLIENT_TRACE_BEGIN(name)
#define SERVICE_CLIENT_TRACE_END()
#define BROKER_TRACE_BEGIN(name)
#define BROKER_TRACE_END()
#define READER_TRACE_BEGIN(name)
#define READER_TRACE_END()

#endif // HAVE_PERFETTO
