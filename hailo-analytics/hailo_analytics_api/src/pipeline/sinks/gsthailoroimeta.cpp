/**
 * @file gsthailoroimeta.cpp
 * @brief GstMeta implementation for HailoROIPtr.
 *
 * Follows the same pattern as gsthailobuffermeta.cpp.
 **/
#include "hailo_analytics/pipeline/sinks/gsthailoroimeta.hpp"

#include <string.h>

static gboolean gst_hailo_roi_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer);
static void gst_hailo_roi_meta_free(GstMeta *meta, GstBuffer *buffer);

GType gst_hailo_roi_meta_api_get_type(void)
{
    static const gchar *tags[] = {NULL};
    static GType type;
    if (g_once_init_enter(const_cast<GType *>(&type)))
    {
        GType _type = gst_meta_api_type_register(GST_HAILO_ROI_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

const GstMetaInfo *gst_hailo_roi_meta_get_info(void)
{
    static const GstMetaInfo *gst_hailo_roi_meta_info = NULL;

    if (g_once_init_enter(&gst_hailo_roi_meta_info))
    {
        const GstMetaInfo *meta = gst_meta_register(
            GST_HAILO_ROI_META_API_TYPE, "GstHailoROIMeta", sizeof(GstHailoROIMeta), gst_hailo_roi_meta_init,
            (GstMetaFreeFunction)gst_hailo_roi_meta_free, (GstMetaTransformFunction) nullptr);
        g_once_init_leave(&gst_hailo_roi_meta_info, meta);
    }
    return gst_hailo_roi_meta_info;
}

// GStreamer allocates GstMeta with POD allocation (like malloc) but HailoROIPtr
// is a shared_ptr (non-POD). The memset ensures no garbage before assigning nullptr.
// Same workaround as gsthailobuffermeta.cpp (MAD-1158).
static gboolean gst_hailo_roi_meta_init(GstMeta *meta, gpointer, GstBuffer *)
{
    GstHailoROIMeta *roi_meta = (GstHailoROIMeta *)meta;
    memset((void *)&roi_meta->roi_ptr, 0, sizeof(roi_meta->roi_ptr));
    roi_meta->roi_ptr = nullptr;
    return TRUE;
}

static void gst_hailo_roi_meta_free(GstMeta *meta, GstBuffer *)
{
    GstHailoROIMeta *roi_meta = (GstHailoROIMeta *)meta;
    roi_meta->roi_ptr = nullptr;
}

GstHailoROIMeta *gst_buffer_add_hailo_roi_meta(GstBuffer *buffer, HailoROIPtr roi_ptr)
{
    g_return_val_if_fail((int)GST_IS_BUFFER(buffer), NULL);

    if (!gst_buffer_is_writable(buffer))
        return NULL;

    GstHailoROIMeta *roi_meta = (GstHailoROIMeta *)gst_buffer_add_meta(buffer, GST_HAILO_ROI_META_INFO, NULL);
    roi_meta->roi_ptr = roi_ptr;
    return roi_meta;
}

GstHailoROIMeta *gst_buffer_get_hailo_roi_meta(GstBuffer *buffer)
{
    return (GstHailoROIMeta *)gst_buffer_get_meta(buffer, GST_HAILO_ROI_META_API_TYPE);
}

gboolean gst_buffer_remove_hailo_roi_meta(GstBuffer *buffer)
{
    g_return_val_if_fail((int)GST_IS_BUFFER(buffer), FALSE);

    GstHailoROIMeta *meta = gst_buffer_get_hailo_roi_meta(buffer);
    if (!meta)
        return TRUE;

    meta->roi_ptr = nullptr;
    if (!gst_buffer_is_writable(buffer))
        return FALSE;

    return gst_buffer_remove_meta(buffer, &meta->meta);
}
