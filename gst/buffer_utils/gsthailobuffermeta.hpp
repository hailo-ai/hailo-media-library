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
#pragma once

#include <gst/gst.h>
#include "media_library/buffer_pool.hpp"

G_BEGIN_DECLS

// Api Type
// First field of gst_meta_register (which returns GstMetaInfo)
// https://gstreamer.freedesktop.org/documentation/gstreamer/gstmeta.html?gi-language=c#gst_meta_register
#define GST_HAILO_BUFFER_META_API_TYPE (gst_hailo_buffer_meta_api_get_type())
#define GST_HAILO_BUFFER_META_INFO (gst_hailo_buffer_meta_get_info())
#define GST_HAILO_BUFFER_META_API_NAME ("GstHailoBufferMetaAPI")

typedef struct _GstHailoBufferMeta GstHailoBufferMeta;

struct _GstHailoBufferMeta
{

    // Required as it is base structure for metadata
    // https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstMeta.html
    GstMeta meta;
    // Custom fields
    HailoMediaLibraryBufferPtr buffer_ptr;
    uint32_t used_size;
};

GType gst_hailo_buffer_meta_api_get_type(void);

GST_EXPORT
const GstMetaInfo *gst_hailo_buffer_meta_get_info(void);

GST_EXPORT
GstHailoBufferMeta *gst_buffer_add_hailo_buffer_meta(GstBuffer *buffer, 
                                                     HailoMediaLibraryBufferPtr buffer_ptr,
                                                     uint32_t used_size);

GST_EXPORT
gboolean gst_buffer_remove_hailo_buffer_meta(GstBuffer *buffer);

GST_EXPORT
GstHailoBufferMeta *gst_buffer_get_hailo_buffer_meta(GstBuffer *b);

G_END_DECLS