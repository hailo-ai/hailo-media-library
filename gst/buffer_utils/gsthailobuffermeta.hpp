/**
* Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
* Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
**/
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