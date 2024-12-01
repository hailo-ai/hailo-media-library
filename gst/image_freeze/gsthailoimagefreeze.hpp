#pragma once
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstbufferpool.h>
#include "media_library/buffer_pool.hpp"

G_BEGIN_DECLS

typedef struct _GstHailoImageFreeze GstHailoImageFreeze;
typedef struct _GstHailoImageFreezeClass GstHailoImageFreezeClass;

#define GST_TYPE_HAILO_IMAGE_FREEZE (gst_hailo_image_freeze_get_type())
#define GST_HAILO_IMAGE_FREEZE(obj)                                                                                    \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_IMAGE_FREEZE, GstHailoImageFreeze))
#define GST_HAILO_IMAGE_FREEZE_CLASS(klass)                                                                            \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_IMAGE_FREEZE, GstHailoImageFreezeClass))
#define GST_IS_HAILO_IMAGE_FREEZE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_IMAGE_FREEZE))
#define GST_IS_HAILO_IMAGE_FREEZE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_IMAGE_FREEZE))

struct _GstHailoImageFreeze
{
    GstElement parent;
    GstPad *sinkpad;
    GstPad *srcpad;

    bool m_freeze;
    GstMapInfo info;
    HailoMediaLibraryBufferPtr frozen_buffer;
    MediaLibraryBufferPoolPtr m_buffer_pool;
};

struct _GstHailoImageFreezeClass
{
    GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_image_freeze_get_type(void);

G_END_DECLS
