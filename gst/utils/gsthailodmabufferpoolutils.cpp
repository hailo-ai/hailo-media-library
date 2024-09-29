#include "gsthailodmabufferpoolutils.hpp"
#include <iostream>

gboolean
gst_is_hailo_dmabuf_pool_type(GstBufferPool *pool)
{
    return GST_IS_HAILO_DMA_BUFFER_POOL(pool);
}

gboolean
gst_hailo_dmabuf_configure_pool(GstDebugCategory *category, GstBufferPool *pool,
                                GstCaps *caps, gsize size, guint min_buffers, guint max_buffers)
{
    GstStructure *config = NULL;

    g_return_val_if_fail(size > 0, FALSE);
    g_return_val_if_fail(max_buffers > 0, FALSE);

    config = gst_buffer_pool_get_config(pool);

    gst_buffer_pool_config_set_params(config, caps, size, min_buffers, max_buffers);

    if (!gst_buffer_pool_set_config(pool, config))
    {
        GST_ERROR_OBJECT(pool, "Unable to set pool configuration");
        gst_object_unref(pool);
        return FALSE;
    }

    if (!gst_buffer_pool_config_validate_params(config, caps, size, min_buffers,
                                                max_buffers))
    {
        GST_ERROR_OBJECT(pool, "Pool configuration validation failed");
        gst_object_unref(pool);
        return FALSE;
    }

    return TRUE;
}

GstBufferPool *
gst_hailo_dma_create_new_pool(GstDebugCategory *category, GstCaps *caps, guint min_buffers,
                              guint max_buffers, gsize size, guint padding)
{
    // Create a new bufferpool object
    GstBufferPool *pool = gst_hailo_dma_buffer_pool_new(padding);
    if (pool == NULL)
    {
        GST_CAT_ERROR(category, "Create Hailo pool failed");
        return NULL;
    }

    // Configure the bufferpool
    gboolean res = gst_hailo_dmabuf_configure_pool(category, pool, caps, size, min_buffers, max_buffers);
    if (res == FALSE)
    {
        GST_ERROR_OBJECT(pool, "Unable to configure pool");
        return NULL;
    }

    gst_caps_unref(caps);
    GST_DEBUG_OBJECT(pool, "Dma-buf bufferpool created with buffer size: %ld min buffers: %d max buffers: %d and padding: %d",
                     size, min_buffers, max_buffers, padding);

    return pool;
}

GstBufferPool *gst_create_hailo_dma_bufferpool_from_caps(GstElement *element, GstCaps *caps, guint bufferpool_min_size, guint bufferpool_max_size)
{
    GstAllocator *allocator = NULL;
    GstBufferPool *pool = NULL;
    GstAllocationParams params;

    gst_allocation_params_init(&params);

    // Get the width and height of the caps
    GstVideoInfo *video_info = gst_video_info_new();
    gst_video_info_from_caps(video_info, caps);

    guint buffer_size = video_info->size;

    // get width and strid from caps
    GstStructure *s = gst_caps_get_structure(caps, 0);
    gint width, stride;
    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "stride", &stride);

    guint padding = stride - width;

    gst_video_info_free(video_info);

    pool = gst_hailo_dma_create_new_pool(GST_CAT_DEFAULT, caps, bufferpool_min_size, bufferpool_max_size, buffer_size, padding);

    if (pool == NULL)
    {
        GST_ERROR_OBJECT(element, "Bufferpool creation from caps - Pool creation failed");
        return NULL;
    }

    if (!gst_buffer_pool_set_active(pool, TRUE))
    {
        GST_ERROR_OBJECT(element, "Bufferpool creation from caps - Unable to set pool active");
        if (allocator)
        {
            gst_object_unref(allocator);
        }
        return NULL;
    }

    if (allocator)
    {
        gst_object_unref(allocator);
    }

    // check if need more things here like in the function above
    return pool;
}