/**
 * @file gsthailoencoder.hpp
 * @brief GStreamer encoder element bound to a MediaLibrary encoded output stream.
 *
 * Provides the @c gsthailoencoder element — a thin GstVideoEncoder that
 * delegates encoding to the MediaLibrary backend identified by @c stream-id.
 * Designed to be used alongside @c gsthailovision in the same pipeline.
 *
 * ## Properties
 * - **stream-id** (string, read-write, READY) — mandatory stream identifier
 *   matching a key in the profile's @c encoded_output_streams map.
 **/
#pragma once

#include <gst/video/gstvideoencoder.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

#define GST_TYPE_HAILO_API_ENCODER (gst_hailo_api_encoder_get_type())
#define GST_HAILO_API_ENCODER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_API_ENCODER, GstHailoApiEncoder))
#define GST_HAILO_API_ENCODER_CLASS(klass)                                                                             \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_API_ENCODER, GstHailoApiEncoderClass))
#define GST_IS_HAILO_API_ENCODER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_API_ENCODER))
#define GST_IS_HAILO_API_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_API_ENCODER))

typedef struct _GstHailoApiEncoder GstHailoApiEncoder;
typedef struct _GstHailoApiEncoderClass GstHailoApiEncoderClass;
typedef struct _GstHailoApiEncoderParams GstHailoApiEncoderParams;

/**
 * @brief GstVideoEncoder subclass that encodes via MediaLibrary.
 *
 * During NULL→READY the element resolves its parent pipeline name and
 * validates the @c stream-id property. On READY→PAUSED it obtains the
 * shared MediaLibrary instance (waiting for the vision element to
 * initialize it) and subscribes to the encoder output callback.
 */
struct __attribute__((visibility("hidden"))) _GstHailoApiEncoder
{
    GstVideoEncoder parent;           ///< Parent GstVideoEncoder instance.
    GstHailoApiEncoderParams *params; ///< Private implementation data.
};

struct _GstHailoApiEncoderClass
{
    GstVideoEncoderClass parent_class;
};

GType gst_hailo_api_encoder_get_type(void);

G_END_DECLS
