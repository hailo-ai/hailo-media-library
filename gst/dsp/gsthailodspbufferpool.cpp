#include <gst/gst.h>
#include <iostream>

#include "dsp/gsthailodspbufferpool.hpp"
#include "dsp/gsthailodsp.h"

G_DEFINE_TYPE(GstHailoDspBufferPool, gst_hailo_dsp_buffer_pool, GST_TYPE_BUFFER_POOL)

static GstFlowReturn gst_hailo_dsp_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **output_buffer_ptr, GstBufferPoolAcquireParams *params);
static void gst_hailo_dsp_buffer_pool_free_buffer(GstBufferPool *pool, GstBuffer *buffer);
static void gst_hailo_dsp_buffer_pool_dispose(GObject *object);

static void
gst_hailo_dsp_buffer_pool_class_init(GstHailoDspBufferPoolClass *klass)
{
    GObjectClass *const object_class = G_OBJECT_CLASS(klass);
    GstBufferPoolClass *const pool_class = GST_BUFFER_POOL_CLASS(klass);

    GST_INFO_OBJECT(object_class, "Hailo DSP buffer pool class init");

    pool_class->alloc_buffer = GST_DEBUG_FUNCPTR(gst_hailo_dsp_buffer_pool_alloc_buffer);
    pool_class->free_buffer = GST_DEBUG_FUNCPTR(gst_hailo_dsp_buffer_pool_free_buffer);
    object_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_dsp_buffer_pool_dispose);
}

static void
gst_hailo_dsp_buffer_pool_dispose(GObject *object)
{
    G_OBJECT_CLASS(gst_hailo_dsp_buffer_pool_parent_class)->dispose(object);
    GstHailoDspBufferPool *pool = GST_HAILO_DSP_BUFFER_POOL(object);
    GST_INFO_OBJECT(pool, "Hailo DSP buffer pool dispose");
    if (pool->config)
    {
        gst_structure_free(pool->config);
        pool->config = NULL;
    }
    // Release DSP device
    dsp_status result = release_device();
    if (result != DSP_SUCCESS)
    {
        GST_ERROR_OBJECT(pool, "Release DSP device failed with status code %d", result);
    }
}

static void
gst_hailo_dsp_buffer_pool_init(GstHailoDspBufferPool *pool)
{
    GST_INFO_OBJECT(pool, "New Hailo DSP buffer pool");
    pool->memory_allocator = &DmaMemoryAllocator::get_instance();
    // Acquire DSP device
    dsp_status status = acquire_device();
    if (status != DSP_SUCCESS)
    {
        GST_ERROR_OBJECT(pool, "Accuire DSP device failed with status code %d", status);
    }
}

GstBufferPool *
gst_hailo_dsp_buffer_pool_new(guint padding)
{
    GstHailoDspBufferPool *pool = GST_HAILO_DSP_BUFFER_POOL(g_object_new(GST_TYPE_HAILO_DSP_BUFFER_POOL, NULL));
    pool->padding = padding;
    pool->config = NULL;
    return GST_BUFFER_POOL_CAST(pool);
}

static GstFlowReturn
gst_hailo_dsp_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **output_buffer_ptr, GstBufferPoolAcquireParams *params)
{
    GstHailoDspBufferPool *hailo_dsp_pool = GST_HAILO_DSP_BUFFER_POOL(pool);
    guint buffer_size=0;
    GstCaps *caps = NULL;

    // Get the size and caps of a buffer from the config of the pool
    if (!hailo_dsp_pool->config)
    {
        hailo_dsp_pool->config = gst_buffer_pool_get_config(pool);
    }
    gst_buffer_pool_config_get_params(hailo_dsp_pool->config, &caps, &buffer_size, NULL, NULL);
    if (caps == NULL)
    {
        GST_ERROR_OBJECT(hailo_dsp_pool, "Failed to get caps from buffer pool config");
        return GST_FLOW_ERROR;
    }

    // Create GstVideoInfo from those caps
    GstVideoInfo *image_info = gst_video_info_new();
    gst_video_info_from_caps(image_info, caps);

    GST_DEBUG_OBJECT(hailo_dsp_pool, "image format %s", image_info->finfo->name);
    GstVideoFormat format = image_info->finfo->format;
    switch (format)
    {
    case GST_VIDEO_FORMAT_RGB:
    {
        // Validate the size of the buffer
        if (buffer_size == 0)
        {
            GST_ERROR_OBJECT(hailo_dsp_pool, "Invalid buffer size");
            return GST_FLOW_ERROR;
        }
        GST_INFO_OBJECT(hailo_dsp_pool, "Allocating buffer of size %d with padding %d", buffer_size, hailo_dsp_pool->padding);

        // Allocate the dma buffer
        void *buffer_ptr = NULL;
        media_library_return ret = hailo_dsp_pool->memory_allocator->allocate_dma_buffer((size_t)buffer_size+hailo_dsp_pool->padding, &buffer_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(pool, "Failed to create buffer with status code %d", ret);
            return GST_FLOW_ERROR;
        }
        GST_INFO_OBJECT(hailo_dsp_pool, "Allocated dma buffer of size %d from dsp memory", buffer_size);

        // Wrap the buffer memory
        void *aligned_buffer_ptr = (void *)(((size_t)buffer_ptr + hailo_dsp_pool->padding));
        *output_buffer_ptr = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                        aligned_buffer_ptr, (size_t)buffer_size, 0, (size_t)buffer_size, NULL, NULL);
        GST_INFO_OBJECT(hailo_dsp_pool, "Allocated buffer memory wrapped");
        break;
    }
    case GST_VIDEO_FORMAT_NV12:
    {
        *output_buffer_ptr = gst_buffer_new();
        for (int i=0; i < 2; i++)
        {
            // Calculate the size of the plane
            size_t channel_size = image_info->stride[i] * image_info->height;
            if (i == 1)
                channel_size /= 2;
            GST_DEBUG_OBJECT(hailo_dsp_pool, "Allocating plane %d buffer of size %ld with padding %d", i, channel_size, hailo_dsp_pool->padding);
            // Allocate the plane buffer
            void *plane_ptr = NULL;
            media_library_return status = hailo_dsp_pool->memory_allocator->allocate_dma_buffer(channel_size, &plane_ptr);
            hailo_dsp_pool->memory_allocator->dmabuf_sync_start(plane_ptr); // start sync so that we can write to it
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                gst_caps_unref(caps);
                GST_ERROR_OBJECT(hailo_dsp_pool, "Error: create_hailo_dsp_buffer - failed to create plane for NV12 buffer");
                return GST_FLOW_ERROR;
            }
            GST_DEBUG_OBJECT(hailo_dsp_pool, "Successfully allocated plane %d buffer of size %ld at address %p", i, channel_size, plane_ptr);
            // Wrap the dma buffer as continuous GstMemory, add the plane to the GstBuffer
            void *aligned_buffer_ptr = (void *)(((size_t)plane_ptr + hailo_dsp_pool->padding));
            GstMemory *mem = gst_memory_new_wrapped(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                    aligned_buffer_ptr,
                                                    channel_size,
                                                    0, channel_size,
                                                    NULL, NULL);
            gst_buffer_insert_memory(*output_buffer_ptr, -1, mem);
        }
        (void)gst_buffer_add_video_meta_full(*output_buffer_ptr,
                                            GST_VIDEO_FRAME_FLAG_NONE,
                                            GST_VIDEO_INFO_FORMAT(image_info),
                                            GST_VIDEO_INFO_WIDTH(image_info),
                                            GST_VIDEO_INFO_HEIGHT(image_info),
                                            GST_VIDEO_INFO_N_PLANES(image_info),
                                            image_info->offset,
                                            image_info->stride);
        break;
    }
    default:
    {
        GST_ERROR_OBJECT(hailo_dsp_pool, "unsupported image format %s", image_info->finfo->name);
        gst_caps_unref(caps);
        return GST_FLOW_ERROR;
    }
    }

    gst_caps_unref(caps);
    return GST_FLOW_OK;
}

static void
gst_hailo_dsp_buffer_pool_free_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
    GstHailoDspBufferPool *hailo_dsp_pool = GST_HAILO_DSP_BUFFER_POOL(pool);
    GST_DEBUG_OBJECT(hailo_dsp_pool, "Freeing buffer %p with padding %d", buffer, hailo_dsp_pool->padding);
    guint memory_count = gst_buffer_n_memory(buffer);
    for (guint i = 0; i < memory_count; i++)
    {
        GstMemory *memory = gst_buffer_peek_memory(buffer, i);
        GstMapInfo memory_map_info;
        gst_memory_map(memory, &memory_map_info, GST_MAP_READ);
        void *buffer_ptr = memory_map_info.data;
        void *aligned_buffer_ptr = (void *)(((size_t)buffer_ptr - hailo_dsp_pool->padding));

        media_library_return result = hailo_dsp_pool->memory_allocator->free_dma_buffer(aligned_buffer_ptr);
        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(hailo_dsp_pool, "Failed to release dma-buf buffer %p number %d out of %d", buffer_ptr, (i+1), memory_count);
        }
        else
        {
            GST_INFO_OBJECT(hailo_dsp_pool, "Released dma-buf buffer %p number %d out of %d", buffer_ptr, (i+1), memory_count);
        }

        gst_memory_unmap(memory, &memory_map_info);
    }

    gst_buffer_unref(buffer);
}
