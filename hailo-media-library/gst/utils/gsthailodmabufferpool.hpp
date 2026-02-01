#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstbufferpool.h>
#include "gsthailodmabufallocator.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_DMA_BUFFER_POOL (gst_hailo_dma_buffer_pool_get_type())
#define GST_HAILO_DMA_BUFFER_POOL(obj)                                                                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DMA_BUFFER_POOL, GstHailoDmaBufferPool))
#define GST_HAILO_DMA_BUFFER_POOL_CLASS(klass)                                                                         \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DMA_BUFFER_POOL, GstHailoDmaBufferPoolClass))
#define GST_IS_HAILO_DMA_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DMA_BUFFER_POOL))
#define GST_IS_HAILO_DMA_BUFFER_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DMA_BUFFER_POOL))

typedef struct _GstHailoDmaBufferPool GstHailoDmaBufferPool;
typedef struct _GstHailoDmaBufferPoolParams GstHailoDmaBufferPoolParams;
typedef struct _GstHailoDmaBufferPoolClass GstHailoDmaBufferPoolClass;

struct _GstHailoDmaBufferPoolParams
{
    guint padding;
    GstStructure *config = nullptr;
    GstHailoDmabufAllocator *memory_allocator = nullptr;

    ~_GstHailoDmaBufferPoolParams()
    {
        if (memory_allocator)
        {
            gst_object_unref(memory_allocator);
            GstHailoDmaHeapControl::decrease_ref_count_dma_ctrl();
            memory_allocator = NULL;
        }
    }
};

struct _GstHailoDmaBufferPool
{
    GstBufferPool parent;
    GstHailoDmaBufferPoolParams *params = nullptr;
};

struct _GstHailoDmaBufferPoolClass
{
    GstBufferPoolClass parent_class;
};

GstBufferPool *gst_hailo_dma_buffer_pool_new(guint padding);

GType gst_hailo_dma_buffer_pool_get_type(void);

G_END_DECLS
