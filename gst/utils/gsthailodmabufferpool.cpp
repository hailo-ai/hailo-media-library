#include <gst/gst.h>
#include <iostream>

#include "utils/gsthailodmabufferpool.hpp"
#include "media_library/media_library_types.hpp"
#include "buffer_utils/buffer_utils.hpp"

G_DEFINE_TYPE(GstHailoDmaBufferPool, gst_hailo_dma_buffer_pool, GST_TYPE_BUFFER_POOL)

static GstFlowReturn gst_hailo_dma_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **output_buffer_ptr,
                                                            GstBufferPoolAcquireParams *params);
static void gst_hailo_dma_buffer_pool_free_buffer(GstBufferPool *pool, GstBuffer *buffer);
static void gst_hailo_dma_buffer_pool_dispose(GObject *object);

#define ALIGNMENT                                                                                                      \
    4095 // The alignment in @params is given as a bitmask so that @align + 1 equals the amount of bytes to align to.

static void gst_hailo_dma_buffer_pool_class_init(GstHailoDmaBufferPoolClass *klass)
{
    GObjectClass *const object_class = G_OBJECT_CLASS(klass);
    GstBufferPoolClass *const pool_class = GST_BUFFER_POOL_CLASS(klass);

    GST_INFO_OBJECT(object_class, "Hailo dma-buf buffer pool class init");

    pool_class->alloc_buffer = GST_DEBUG_FUNCPTR(gst_hailo_dma_buffer_pool_alloc_buffer);
    pool_class->free_buffer = GST_DEBUG_FUNCPTR(gst_hailo_dma_buffer_pool_free_buffer);
    object_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_dma_buffer_pool_dispose);
}

static void gst_hailo_dma_buffer_pool_dispose(GObject *object)
{
    G_OBJECT_CLASS(gst_hailo_dma_buffer_pool_parent_class)->dispose(object);
    GstHailoDmaBufferPool *pool = GST_HAILO_DMA_BUFFER_POOL(object);
    GST_INFO_OBJECT(pool, "Hailo dma-buf buffer pool dispose");

    if (pool->memory_allocator)
    {
        gst_object_unref(pool->memory_allocator);
        GstHailoDmaHeapControl::decrease_ref_count_dma_ctrl();
        pool->memory_allocator = NULL;
    }
}

static void gst_hailo_dma_buffer_pool_init(GstHailoDmaBufferPool *pool)
{
    GST_INFO_OBJECT(pool, "New Hailo dma-buf buffer pool");
    gchar *name = g_strdup("hailo_allocator");
    pool->memory_allocator = GST_HAILO_DMABUF_ALLOCATOR(g_object_new(GST_TYPE_HAILO_DMABUF_ALLOCATOR, NULL));
    GstHailoDmaHeapControl::increase_ref_count_dma_ctrl();
    g_free(name);
}

GstBufferPool *gst_hailo_dma_buffer_pool_new(guint padding)
{
    GstHailoDmaBufferPool *pool = GST_HAILO_DMA_BUFFER_POOL(g_object_new(GST_TYPE_HAILO_DMA_BUFFER_POOL, NULL));
    pool->padding = padding;
    pool->config = NULL;
    return GST_BUFFER_POOL_CAST(pool);
}

static GstFlowReturn gst_hailo_dma_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **output_buffer_ptr,
                                                            GstBufferPoolAcquireParams *)
{
    GstHailoDmaBufferPool *hailo_dmabuf_pool = GST_HAILO_DMA_BUFFER_POOL(pool);
    guint buffer_size = 0;
    GstCaps *caps = NULL;

    // Get the size and caps of a buffer from the config of the pool
    if (!hailo_dmabuf_pool->config)
    {
        hailo_dmabuf_pool->config = gst_buffer_pool_get_config(pool);
    }
    gst_buffer_pool_config_get_params(hailo_dmabuf_pool->config, &caps, &buffer_size, NULL, NULL);
    if (caps == NULL)
    {
        GST_ERROR_OBJECT(hailo_dmabuf_pool, "Failed to get caps from buffer pool config");
        return GST_FLOW_ERROR;
    }

    // Create GstVideoInfo from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);

    GST_DEBUG_OBJECT(hailo_dmabuf_pool, "image format %s", image_info->finfo->name);
    GstVideoFormat format = image_info->finfo->format;
    switch (format)
    {
    case GST_VIDEO_FORMAT_RGB: {
        // Validate the size of the buffer
        if (buffer_size == 0)
        {
            GST_ERROR_OBJECT(hailo_dmabuf_pool, "Invalid buffer size");
            return GST_FLOW_ERROR;
        }
        GST_INFO_OBJECT(hailo_dmabuf_pool, "Allocating buffer of size %d with padding %d", buffer_size,
                        hailo_dmabuf_pool->padding);

        GstAllocationParams alloc_params = {
            (GstMemoryFlags)(GST_MEMORY_FLAG_ZERO_PREFIXED | GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS), ALIGNMENT, 0,
            hailo_dmabuf_pool->padding, 0};
        *output_buffer_ptr = gst_buffer_new_allocate((GstAllocator *)hailo_dmabuf_pool->memory_allocator,
                                                     (size_t)buffer_size, &alloc_params);

        if (!*output_buffer_ptr)
        {
            return GST_FLOW_ERROR;
        }
        GST_INFO_OBJECT(hailo_dmabuf_pool, "Allocated dma buff buffer RGB");
        break;
    }

    case GST_VIDEO_FORMAT_NV12: {
        *output_buffer_ptr = gst_buffer_new();
        GstAllocationParams alloc_params = {
            (GstMemoryFlags)(GST_MEMORY_FLAG_ZERO_PREFIXED | GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS), ALIGNMENT, 0,
            hailo_dmabuf_pool->padding, 0};

        for (int i = 0; i < 2; i++)
        {
            // Calculate the size of the plane
            size_t channel_size = image_info->stride[i] * image_info->height;
            if (i == 1)
                channel_size /= 2;
            GST_DEBUG_OBJECT(hailo_dmabuf_pool, "Allocating plane %d buffer of size %ld with padding %d", i,
                             channel_size, hailo_dmabuf_pool->padding);

            GstMemory *mem =
                gst_allocator_alloc((GstAllocator *)hailo_dmabuf_pool->memory_allocator, channel_size, &alloc_params);
            GST_DEBUG_OBJECT(hailo_dmabuf_pool, "Successfully allocated plane %d buffer of size %ld at address %p", i,
                             channel_size, mem);
            gst_buffer_insert_memory(*output_buffer_ptr, -1, mem); // Insert the memory into the buffer at the end
        }

        add_video_meta_to_buffer(*output_buffer_ptr, image_info);
        break;
    }

    default: {
        GST_ERROR_OBJECT(hailo_dmabuf_pool, "unsupported image format %s", image_info->finfo->name);
        return GST_FLOW_ERROR;
    }
    }

    return GST_FLOW_OK;
}

static void gst_hailo_dma_buffer_pool_free_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
    GstHailoDmaBufferPool *hailo_dmabuf_pool = GST_HAILO_DMA_BUFFER_POOL(pool);
    GST_DEBUG_OBJECT(hailo_dmabuf_pool, "Freeing buffer %p with padding %d", buffer, hailo_dmabuf_pool->padding);
    gst_buffer_unref(buffer);
}
