#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstbufferpool.h>
#include "media_library/dma_memory_allocator.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_DSP_BUFFER_POOL (gst_hailo_dsp_buffer_pool_get_type())
#define GST_HAILO_DSP_BUFFER_POOL(obj)                                                                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DSP_BUFFER_POOL, GstHailoDspBufferPool))
#define GST_HAILO_DSP_BUFFER_POOL_CLASS(klass)                                                                         \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DSP_BUFFER_POOL, GstHailoDspBufferPoolClass))
#define GST_IS_HAILO_DSP_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DSP_BUFFER_POOL))
#define GST_IS_HAILO_DSP_BUFFER_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DSP_BUFFER_POOL))

typedef struct _GstHailoDspBufferPool GstHailoDspBufferPool;
typedef struct _GstHailoDspBufferPoolParams GstHailoDspBufferPoolParams;
typedef struct _GstHailoDspBufferPoolClass GstHailoDspBufferPoolClass;

struct _GstHailoDspBufferPoolParams
{
    guint padding;
    GstStructure *config = nullptr;
    DmaMemoryAllocator *memory_allocator = nullptr;

    ~_GstHailoDspBufferPoolParams()
    {
        if (config)
        {
            gst_structure_free(config);
        }
    }
};

struct _GstHailoDspBufferPool
{
    GstBufferPool parent;
    GstHailoDspBufferPoolParams *params = nullptr;
};

struct _GstHailoDspBufferPoolClass
{
    GstBufferPoolClass parent_class;
};

GstBufferPool *gst_hailo_dsp_buffer_pool_new(guint padding);

GType gst_hailo_dsp_buffer_pool_get_type(void);

G_END_DECLS
