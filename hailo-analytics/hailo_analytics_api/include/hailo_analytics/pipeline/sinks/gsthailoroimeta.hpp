/**
 * @file gsthailoroimeta.hpp
 * @brief GstMeta carrying an HailoROIPtr on a GstBuffer.
 *
 * Allows analytics detection/classification results (HailoROI) to travel
 * alongside a GstBuffer through the GStreamer pipeline. Follows the same
 * registration pattern as GstHailoBufferMeta.
 **/
#pragma once

#include <gst/gst.h>
#include <glib-object.h>
#include <glib.h>
#include <gst/gstmeta.h>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

G_BEGIN_DECLS

#define GST_HAILO_ROI_META_API_TYPE (gst_hailo_roi_meta_api_get_type())
#define GST_HAILO_ROI_META_INFO (gst_hailo_roi_meta_get_info())
#define GST_HAILO_ROI_META_API_NAME ("GstHailoROIMetaAPI")

typedef struct _GstHailoROIMeta GstHailoROIMeta;

struct _GstHailoROIMeta
{
    GstMeta meta;
    HailoROIPtr roi_ptr;
};

GType gst_hailo_roi_meta_api_get_type(void);

GST_EXPORT
const GstMetaInfo *gst_hailo_roi_meta_get_info(void);

GST_EXPORT
GstHailoROIMeta *gst_buffer_add_hailo_roi_meta(GstBuffer *buffer, HailoROIPtr roi_ptr);

GST_EXPORT
GstHailoROIMeta *gst_buffer_get_hailo_roi_meta(GstBuffer *buffer);

GST_EXPORT
gboolean gst_buffer_remove_hailo_roi_meta(GstBuffer *buffer);

G_END_DECLS
