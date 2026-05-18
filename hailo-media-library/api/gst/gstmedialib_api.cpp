/**
 * @file gstmedialib_api.cpp
 * @brief GStreamer plugin entry point for the MediaLibrary API elements.
 *
 * Registers the @c gsthailovision and @c gsthailoencoder elements
 * into the @c medialib_api plugin.
 **/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "vision/gsthailovision.hpp"
#include "encoder/gsthailoencoder.hpp"

static gboolean medialib_api_plugin_init(GstPlugin *plugin)
{
    gst_element_register(plugin, "gsthailovision", GST_RANK_PRIMARY, GST_TYPE_HAILO_VISION);
    gst_element_register(plugin, "gsthailoencoder", GST_RANK_PRIMARY, GST_TYPE_HAILO_API_ENCODER);
    return TRUE;
}

GST_PLUGIN_DEFINE(GST_MEDIALIB_MAJOR_VERSION, GST_MEDIALIB_MINOR_VERSION, medialib_api,
                  "Hailo Media Library API plugin", medialib_api_plugin_init, VERSION, "unknown", PACKAGE,
                  "https://hailo.ai/")
