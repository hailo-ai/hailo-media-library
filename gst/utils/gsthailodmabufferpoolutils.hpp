#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstbufferpool.h>
#include "utils/gsthailodmabufferpool.hpp"

G_BEGIN_DECLS

GstBufferPool *gst_hailo_dma_create_new_pool(GstDebugCategory *category, GstCaps *caps, guint min_buffers,
                                             guint max_buffers, gsize size, guint padding);
/**
 * Adds a new hailo buffer pool to the query
 * @param[in] category: Category to use for debug messages
 * @param[in] query: Query to parse caps from
 * @param[in] min_buffers: Minimum size buffers in the pool
 * @param[in] max_buffers: Maximum size buffers in the pool
 * @param[in] size: Maximum size buffers in the pool
 *
 * @return GstBufferPool
 *
 */
GstBufferPool *gst_hailo_dmabuf_create_new_pool(GstDebugCategory *category, GstQuery *query, guint min_buffers,
                                                guint max_buffers, gsize size);

/**
 * Configures a hailo buffer pool
 * @param[in] category: Category to use for debug messages
 * @param[in] pool: Pool to configure
 * @param[in] caps: Caps to use for the pool
 * @param[in] size: size of buffers in the pool
 * @param[in] min_buffers: Minimum number of buffers in the pool
 * @param[in] max_buffers: Maximum number of buffers in the pool
 *
 * @return boolean - pool was successfully configured
 *
 */
gboolean gst_hailo_dmabuf_configure_pool(GstBufferPool *pool, GstCaps *caps, gsize size, guint min_buffers,
                                         guint max_buffers);

/**
 * Checks if the pool is a hailo pool
 * @param[in] pool: bufferpool to check
 *
 * @return boolean - pool is a hailo pool
 *
 */
gboolean gst_is_hailo_dmabuf_pool_type(GstBufferPool *pool);

/**
 * Creates a hailo bufferpool from an allocation query, configure it, activate, and add it to the query
 * @param[in] element: Element to use for debug messages
 * @param[in] query: Query to check, get caps from, and add the pool to
 * @param[in] bufferpool_max_size: Maximum size of the bufferpool
 * @return GstBufferPool
 */
GstBufferPool *
// gst_create_hailo_dma_bufferpool_from_allocation_query(GstElement *element, GstQuery *query, guint
// bufferpool_min_size, guint bufferpool_max_size, guint padding);
gst_create_hailo_dma_bufferpool_from_allocation_query(GstElement *element, GstQuery *query, guint bufferpool_min_size,
                                                      guint bufferpool_max_size);
GstBufferPool *gst_create_hailo_dma_bufferpool_from_caps(GstElement *element, GstCaps *caps, guint bufferpool_min_size,
                                                         guint bufferpool_max_size);

G_END_DECLS
