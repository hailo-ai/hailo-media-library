/*
 * Copyright (c) 2021-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL 2.1 license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <linux/dma-heap.h>
#include <mutex>
#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS

#define GST_TYPE_HAILO_DMABUF_ALLOCATOR (gst_hailo_dmabuf_allocator_get_type())
#define GST_HAILO_DMABUF_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DMABUF_ALLOCATOR, GstHailoDmabufAllocator))
#define GST_HAILO_DMABUF_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DMABUF_ALLOCATOR, GstHailoDmabufAllocator))
#define GST_IS_HAILO_DMABUF_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DMABUF_ALLOCATOR))
#define GST_IS_HAILO_DMABUF_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DMABUF_ALLOCATOR))

#define GST_HAILO_USE_DMA_BUFFER_ENV_VAR "GST_HAILO_USE_DMA_BUFFER"

class GstHailoDmaHeapControl
{
public:
    static bool dma_heap_fd_open;
    static int dma_heap_fd;
    static uint ref_count;
    static std::mutex mutex;

    static void increase_ref_count_dma_ctrl() // move this inside the class
    {
        std::unique_lock<std::mutex> lock(mutex);
        ref_count++;
    }

    static void decrease_ref_count_dma_ctrl() // move this inside the class
    {
        std::unique_lock<std::mutex> lock(mutex);
        ref_count--;
        if (ref_count == 0)
        {
            close(dma_heap_fd);
            dma_heap_fd_open = false;
        }
    }
};

struct GstHailoDmabufAllocator
{
    GstDmaBufAllocator parent;
};

struct GstHailoDmabufAllocatorClass
{
    GstDmaBufAllocatorClass parent;
};


GType gst_hailo_dmabuf_allocator_get_type(void);

G_END_DECLS
