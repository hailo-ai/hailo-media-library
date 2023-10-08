/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "gsthailobuffermeta.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gboolean gst_hailo_buffer_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer);
static void gst_hailo_buffer_meta_free(GstMeta *meta, GstBuffer *buffer);
static gboolean gst_hailo_buffer_meta_transform(GstBuffer *transbuf, GstMeta *meta, GstBuffer *buffer,
                                         GQuark type, gpointer data);

// Register metadata type and returns Gtype
// https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstMeta.html#gst-meta-api-type-register
GType gst_hailo_buffer_meta_api_get_type(void)
{
    static const gchar *tags[] = {NULL};
    static volatile GType type;
    if (g_once_init_enter(const_cast<GType *>(&type)))
    {
        GType _type = gst_meta_api_type_register(GST_HAILO_BUFFER_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

// GstMetaInfo provides info for specific metadata implementation
// https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstMeta.html#GstMetaInfo
const GstMetaInfo *gst_hailo_buffer_meta_get_info(void)
{
    static const GstMetaInfo *gst_hailo_buffer_meta_info = NULL;

    if (g_once_init_enter(&gst_hailo_buffer_meta_info))
    {
        // Explanation of fields
        // https://gstreamer.freedesktop.org/documentation/design/meta.html#gstmeta1
        const GstMetaInfo *meta = gst_meta_register(GST_HAILO_BUFFER_META_API_TYPE, /* api type */
                                                    "GstHailoBufferMeta",          /* implementation type */
                                                    sizeof(GstHailoBufferMeta),    /* size of the structure */
                                                    gst_hailo_buffer_meta_init,
                                                    (GstMetaFreeFunction)gst_hailo_buffer_meta_free,
                                                    gst_hailo_buffer_meta_transform);
        g_once_init_leave(&gst_hailo_buffer_meta_info, meta);
    }
    return gst_hailo_buffer_meta_info;
}

// Meta init function
// Fourth field in GstMetaInfo
// https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstMeta.html#GstMetaInitFunction
static gboolean gst_hailo_buffer_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    GstHailoBufferMeta *gst_hailo_buffer_meta = (GstHailoBufferMeta *)meta;
    // GStreamer is allocating the GstHailoBufferMeta struct with POD allocation (like malloc) when
    // it holds non POD type (shared_ptr). The memset assures there is no garbage data in this address.
    // This is a temporary solution because memset to non POD type is undefined behavior.
    // https://stackoverflow.com/questions/59747240/is-it-okay-to-memset-a-struct-which-has-an-another-struct-with-smart-pointer-mem?rq=1
    // Opened an issue to replace this line with right initialization - MAD-1158.
    memset((void *)&gst_hailo_buffer_meta->buffer_ptr, 0, sizeof(gst_hailo_buffer_meta->buffer_ptr));
    gst_hailo_buffer_meta->buffer_ptr = nullptr;
    gst_hailo_buffer_meta->used_size = 0;
    return TRUE;
}

// Meta free function
// Fifth field in GstMetaInfo
// https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstMeta.html#GstMetaFreeFunction
static void gst_hailo_buffer_meta_free(GstMeta *meta, GstBuffer *buffer)
{
    GstHailoBufferMeta *gst_hailo_buffer_meta = (GstHailoBufferMeta *)meta;
    if (gst_hailo_buffer_meta->buffer_ptr)
        gst_hailo_buffer_meta->buffer_ptr->decrease_ref_count();
    gst_hailo_buffer_meta->buffer_ptr = nullptr;
    gst_hailo_buffer_meta->used_size = 0;
}

// Meta transform function
// Sixth field in GstMetaInfo
// https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstMeta.html#GstMetaTransformFunction
static gboolean gst_hailo_buffer_meta_transform(GstBuffer *transbuf, GstMeta *meta, GstBuffer *buffer,
                                         GQuark type, gpointer data)
{
    GstHailoBufferMeta *gst_hailo_buffer_meta = (GstHailoBufferMeta *)meta;
    HailoMediaLibraryBufferPtr buffer_ptr = gst_hailo_buffer_meta->buffer_ptr;
    uint32_t used_size = gst_hailo_buffer_meta->used_size;

    GstHailoBufferMeta *new_hailo_meta = gst_buffer_add_hailo_buffer_meta(transbuf, buffer_ptr, used_size);
    if (gst_buffer_is_writable(buffer))
        gst_buffer_remove_meta(buffer, &gst_hailo_buffer_meta->meta);

    if(!new_hailo_meta)
    {
        GST_ERROR("gst_hailo_buffer_meta_transform: failed to transform hailo_meta");
        return FALSE;
    }

    return TRUE;
}

GstHailoBufferMeta *gst_buffer_get_hailo_meta(GstBuffer *buffer)
{
    // https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstBuffer.html#gst-buffer-get-meta
    GstHailoBufferMeta *meta = (GstHailoBufferMeta *)gst_buffer_get_meta((buffer), GST_HAILO_BUFFER_META_API_TYPE);
    return meta;
}
/**
 * @brief Addes a new GstHailoBufferMeta to a given buffer, this meta is initialized with a given HailoMediaLibraryBufferPtr.
 *
 * @param buffer Buffer to add the metadata on.
 * @param buffer_ptr HailoMediaLibraryBufferPtr to initialize the meta with
 * @return GstHailoBufferMeta* The meta structure that was added to the buffer.
 */
GstHailoBufferMeta *gst_buffer_add_hailo_buffer_meta(GstBuffer *buffer, 
                                                     HailoMediaLibraryBufferPtr buffer_ptr,
                                                     uint32_t used_size)
{
    GstHailoBufferMeta *gst_hailo_buffer_meta = NULL;

    // check that gst_buffer valid
    g_return_val_if_fail((int)GST_IS_BUFFER(buffer), NULL);

    // check that gst_buffer writable
    if (!gst_buffer_is_writable(buffer))
        return gst_hailo_buffer_meta;

    // https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstBuffer.html#gst-buffer-add-meta
    gst_hailo_buffer_meta = (GstHailoBufferMeta *)gst_buffer_add_meta(buffer, GST_HAILO_BUFFER_META_INFO, NULL);

    if (buffer_ptr)
        buffer_ptr->increase_ref_count();

    gst_hailo_buffer_meta->buffer_ptr = buffer_ptr;
    gst_hailo_buffer_meta->used_size = used_size;

    return gst_hailo_buffer_meta;
}

/**
 * @brief  Removes GstHailoBufferMeta from a given buffer.
 *
 * @param buffer A buffer to remove meta from.
 * @return gboolean whether removal was successfull (TRUE if there isn't GstHailoBufferMeta).
 * @note Removes only the first GstHailoBufferMeta in this buffer.
 */
gboolean gst_buffer_remove_hailo_meta(GstBuffer *buffer)
{
    g_return_val_if_fail((int)GST_IS_BUFFER(buffer), false);

    GstHailoBufferMeta *meta = (GstHailoBufferMeta *)gst_buffer_get_meta((buffer), GST_HAILO_BUFFER_META_API_TYPE);

    if (meta == NULL)
        return TRUE;

    meta->buffer_ptr = nullptr;
    if (!gst_buffer_is_writable(buffer))
        return FALSE;

    // https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstBuffer.html#gst-buffer-remove-meta
    return gst_buffer_remove_meta(buffer, &meta->meta);
}


/**
 * @brief Returns the GstHailoBufferMeta from a given buffer.
 *
 * @param buffer A buffer to get meta from.
 * @return GstHailoBufferMeta* The meta structure that was added to the buffer.
 */
GstHailoBufferMeta *gst_buffer_get_hailo_buffer_meta(GstBuffer *buffer)
{
    // https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstBuffer.html#gst-buffer-get-meta
    GstHailoBufferMeta *meta = (GstHailoBufferMeta *)gst_buffer_get_meta((buffer), GST_HAILO_BUFFER_META_API_TYPE);
    return meta;
}