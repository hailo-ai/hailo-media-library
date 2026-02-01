#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"

HAILO_PERFETTO_INITIALIZER(hailo_analytics_perfetto, HAILO_ANALYTICS_TRACK, HAILO_ANALYTICS_FRAMERATE_TRACK,
                           HAILO_ANALYTICS_QUEUE_LEVEL_TRACK, HAILO_ANALYTICS_PROCESSING_TRACK);
