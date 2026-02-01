/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/**
 * @file dma_memory_allocator.hpp
 * @brief MediaLibrary Module for allocating memory using dma-heap
 **/

#pragma once

#include <mutex>
#include <memory>
#include <stdint.h>
#include <unordered_map>
#include <string_view>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include "media_library_types.hpp"

#define MIN_FD_RANGE 1024

class DmaMemoryAllocator
{
  private:
    uint fd_count;
    int m_dma_heap_fd;
    bool m_dma_heap_fd_open;
    bool m_should_fd_dup;
    std::shared_ptr<std::mutex> m_allocator_mutex;
    std::unordered_map<void *, dma_heap_allocation_data> m_allocated_buffers;
    std::unordered_map<void *, dma_heap_allocation_data> m_external_buffers;
    DmaMemoryAllocator();
    ~DmaMemoryAllocator();

    media_library_return dmabuf_fd_open();
    media_library_return dmabuf_fd_close();
    media_library_return dmabuf_map(dma_heap_allocation_data &heap_data, void **mapped_memory);
    media_library_return dmabuf_heap_alloc(dma_heap_allocation_data &heap_data, uint size,
                                           uint min_fd_range = MIN_FD_RANGE);
    media_library_return dmabuf_sync(void *buffer, dma_buf_sync &sync);
    media_library_return dmabuf_sync(int fd, dma_buf_sync &sync);

  public:
    static DmaMemoryAllocator &get_instance()
    {
        static DmaMemoryAllocator instance;
        return instance;
    }

    DmaMemoryAllocator(DmaMemoryAllocator const &) = delete;
    void operator=(DmaMemoryAllocator const &) = delete;
    void free(void *buffer);

    media_library_return allocate_dma_buffer(uint size, void **buffer);
    media_library_return free_dma_buffer(void *buffer);
    media_library_return map_external_dma_buffer(uint size, uint fd, void **buffer);
    media_library_return unmap_external_dma_buffer(void *buffer);
    media_library_return dmabuf_sync_start(void *buffer);
    media_library_return dmabuf_sync_start(int fd);
    media_library_return dmabuf_sync_end(void *buffer);
    media_library_return dmabuf_sync_end(int fd);
    media_library_return get_fd(void *buffer, int &fd, bool include_external = true);
    media_library_return get_ptr(uint fd, void **buffer, bool include_external = true);
    size_t get_free_memory_mb();
};

static inline void destroy_dma_buffer(void *buffer)
{
    DmaMemoryAllocator::get_instance().free_dma_buffer(buffer);
}
