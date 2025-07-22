#include <mutex>
#include <thread>
#include "hailo_media_library_perfetto.hpp"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

class PerfettoInitializer
{
  private:
    template <typename TrackType> static void InitCustomTrack(TrackType track)
    {
        /*
          I suspect we need this only because of a bug in Perfetto in the function WriteTrackDescriptorIfNeeded
          that does not emit a track_descriptor if the track is not in the TrackRegistry.
          I think the function should have dynamically added the tracks to the registry instead of
          asking the library users to manually add the tracks by calling SetTrackDescriptor...
        */
        perfetto::protos::gen::TrackDescriptor desc = track.Serialize();
        perfetto::TrackEvent::SetTrackDescriptor(track, desc);
    }

  public:
    PerfettoInitializer()
    {
        perfetto::TracingInitArgs args;
        args.backends = perfetto::kSystemBackend;
        perfetto::Tracing::Initialize(args);
        perfetto::TrackEvent::Register();
        /* Register all tracks that are used as parents and not as direct value/event tracks. */
        InitCustomTrack(MEDIA_LIBRARY_TRACK);
        InitCustomTrack(BUFFER_POOLS_TRACK);
        InitCustomTrack(DENOISE_TRACK);
        InitCustomTrack(VIDEO_DEV_TRACK);
        InitCustomTrack(HDR_TRACK);
        InitCustomTrack(DSP_OPS_TRACK);
    }

    ~PerfettoInitializer()
    {
        perfetto::TrackEvent::Flush();
        perfetto::Tracing::Shutdown();
    }
};

// This static instance ensures the constructor runs during media library load
static PerfettoInitializer _perfetto_initializer;
