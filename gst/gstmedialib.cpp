/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "buffer_utils/gsthailobuffermeta.hpp"
#include "encoder/gsthailoh264enc.hpp"
#include "encoder/gsthailoh265enc.hpp"
#include "hailo_encoder/gsthailoencoder.hpp"
#include "osd/gsthailoosd.hpp"
#include "jpeg/gsthailojpeg.hpp"
#include "encodebin/gsthailoencodebin.hpp"
#include "visionpreproc/gsthailovisionpreproc.hpp"
#include "multi_resize/gsthailomultiresize.hpp"
#include "dewarp/gsthailodewarp.hpp"
#include "denoise/gsthailodenoise.hpp"
#include "defog/gsthailodefog.hpp"
#include "frontend/gsthailofrontend.hpp"
#include "frontend/gsthailofrontendbinsrc.hpp"
#include "upload/gsthailoupload.hpp"
#include <gst/gst.h>

static gboolean
media_library_plugin_init(GstPlugin *plugin)
{
    gst_element_register(plugin, "hailovisionpreproc", GST_RANK_NONE, GST_TYPE_HAILO_VISION_PREPROC);
    gst_element_register(plugin, "hailoh265enc", GST_RANK_PRIMARY, GST_TYPE_HAILO_H265_ENC);
    gst_element_register(plugin, "hailoh264enc", GST_RANK_PRIMARY, GST_TYPE_HAILO_H264_ENC);
    gst_element_register(plugin, "hailoosd", GST_RANK_PRIMARY, GST_TYPE_HAILO_OSD);
    gst_element_register(plugin, "hailoencoder", GST_RANK_PRIMARY, GST_TYPE_HAILO_ENCODER);
    gst_element_register(plugin, "hailoencodebin", GST_RANK_PRIMARY, GST_TYPE_HAILO_ENCODE_BIN);
    gst_element_register(plugin, "hailojpegenc", GST_RANK_PRIMARY, GST_TYPE_HAILOJPEGENC);
    gst_element_register(plugin, "hailomultiresize", GST_RANK_PRIMARY, GST_TYPE_HAILO_MULTI_RESIZE);
    gst_element_register(plugin, "hailodewarp", GST_RANK_PRIMARY, GST_TYPE_HAILO_DEWARP);
    gst_element_register(plugin, "hailodenoise", GST_RANK_PRIMARY, GST_TYPE_HAILO_DENOISE);
    gst_element_register(plugin, "hailodefog", GST_RANK_PRIMARY, GST_TYPE_HAILO_DEFOG);
    gst_element_register(plugin, "hailofrontend", GST_RANK_PRIMARY, GST_TYPE_HAILO_FRONTEND);
    gst_element_register(plugin, "hailofrontendbinsrc", GST_RANK_PRIMARY, GST_TYPE_HAILO_FRONTEND_BINSRC);
    gst_element_register(plugin, "hailoupload", GST_RANK_PRIMARY, GST_TYPE_HAILO_UPLOAD);
    gst_hailo_buffer_meta_get_info();
    gst_hailo_buffer_meta_api_get_type();
    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, medialib, "Hailo Media Library plugin", media_library_plugin_init,
                  VERSION, "unknown", PACKAGE, "https://hailo.ai/")
