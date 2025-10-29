#include "hailo_media_library_perfetto.hpp"

HAILO_PERFETTO_INITIALIZER(media_library_perfetto, MEDIA_LIBRARY_TRACK, BUFFER_POOLS_TRACK, DENOISE_TRACK,
                           VIDEO_DEV_TRACK, HDR_TRACK, DSP_OPS_TRACK);
