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

#include "dma_memory_allocator.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "common.hpp"
#include "env_vars.hpp"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>

#define DEVPATH "/dev/dma_heap/hailo_media_buf,cma"

DmaMemoryAllocator::DmaMemoryAllocator()
{
    fd_count = 0;
    m_allocator_mutex = std::make_shared<std::mutex>();
    m_dma_heap_fd_open = false;
    if (dmabuf_fd_open() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("dmabuf_fd_open failed!");
    }

    m_should_fd_dup = is_env_variable_on(MEDIALIB_FD_DUP_ENV_VAR);

    if (m_should_fd_dup)
    {
        LOGGER__INFO("F_DUPFD is enabled");
    }
}

DmaMemoryAllocator::~DmaMemoryAllocator()
{
    dmabuf_fd_close();
}

media_library_return DmaMemoryAllocator::dmabuf_fd_open()
{
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    if (m_dma_heap_fd_open)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__DEBUG("dmabuf_fd_open function-start");

    m_dma_heap_fd = open(DEVPATH, O_RDWR | O_CLOEXEC);
    if (m_dma_heap_fd < 0)
    {
        LOGGER__ERROR("open fd failed!");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    m_dma_heap_fd_open = true;
    LOGGER__DEBUG("dmabuf_fd_open function-end");

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::dmabuf_fd_close()
{
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    if (m_allocated_buffers.size() > 0)
    {
        LOGGER__ERROR("allocated buffers not freed");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    if (m_dma_heap_fd_open)
    {
        LOGGER__DEBUG("fd is open, closing");
        close(m_dma_heap_fd);
        m_dma_heap_fd_open = false;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::dmabuf_heap_alloc(dma_heap_allocation_data &heap_data, uint size,
                                                           uint min_fd_range)
{
    LOGGER__DEBUG("dmabuf_heap_alloc function-start: heap_data.fd = {}, heap_data.len = {}", heap_data.fd,
                  heap_data.len);

    heap_data = {
        .len = size,
        .fd = 0, // fd is an output parameter
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };

    int ret = ioctl(m_dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data);
    if (ret < 0)
    {
        LOGGER__ERROR("ioctl DMA_HEAP_IOCTL_ALLOC failed!");

        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    if (!m_should_fd_dup)
    {
        LOGGER__DEBUG("dmabuf_heap_alloc heap_data.fd = {}, heap_data.len = {}", heap_data.fd, heap_data.len);
        return MEDIA_LIBRARY_SUCCESS;
    }

    // F_DUPFD is used to ensure that applications running in a single process do not run out of available file
    // descriptors. In Linux, a process has a limit of 1024 file descriptors. and some legacy applications might only
    // use the first 1023 file descriptors.
    int new_fd = fcntl(heap_data.fd, F_DUPFD, min_fd_range);
    if (new_fd < 0)
    {
        LOGGER__ERROR("F_DUPFD failed for fd = {} and new_fd = {} with error = {}", heap_data.fd, new_fd, errno);
        close(heap_data.fd);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    close(heap_data.fd);
    heap_data.fd = new_fd;

    LOGGER__DEBUG("dmabuf_heap_alloc heap_data.fd = {}, heap_data.len = {}", heap_data.fd, heap_data.len);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::unmap_external_dma_buffer(void *buffer)
{
    LOGGER__DEBUG("unmap external dma buffer function-start: buffer = {}", fmt::ptr(buffer));
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);

    if (m_external_buffers.find(buffer) == m_external_buffers.end())
    {
        LOGGER__DEBUG("buffer {} not found in m_external_buffers", fmt::ptr(buffer));
        return MEDIA_LIBRARY_SUCCESS;
    }

    auto length = m_external_buffers[buffer].len;
    if (munmap(buffer, length) == -1)
    {
        LOGGER__ERROR("munmap failed for external buffer = {}!", fmt::ptr(buffer));
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    m_external_buffers.erase(buffer);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::map_external_dma_buffer(uint size, uint fd, void **buffer)
{
    LOGGER__DEBUG("map external dma buffer function-start: size = {}", size);

    if (get_ptr(fd, buffer) == MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__DEBUG("buffer already exists (fd = {})", fd);
        return MEDIA_LIBRARY_SUCCESS;
    }

    std::unique_lock<std::mutex> lock(*m_allocator_mutex);

    dma_heap_allocation_data heap_data = {
        .len = size,
        .fd = fd,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };

    if (dmabuf_map(heap_data, buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("dmabuf_map failed!");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    m_external_buffers[*buffer] = heap_data;

    LOGGER__DEBUG("map_dma_buffer function-end: buffer = {}, size = {}", fmt::ptr(*buffer), size);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::dmabuf_map(dma_heap_allocation_data &heap_data, void **mapped_memory)
{
    LOGGER__DEBUG("dmabuf_map start: heap_data.fd = {}, heap_data.len = {}", heap_data.fd, heap_data.len);

    *mapped_memory = mmap(NULL, heap_data.len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, heap_data.fd, 0);

    if (*mapped_memory == MAP_FAILED)
    {
        LOGGER__ERROR("dmabuf map failed, errno = {}", strerror(errno));
        close(heap_data.fd);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__DEBUG("dmabuf_map end: heap_data.fd = {}, heap_data.len = {}", heap_data.fd, heap_data.len);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::allocate_dma_buffer(uint size, void **buffer)
{
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    LOGGER__DEBUG("allocating dma buffer function-start: buffer = {}, size = {}", fmt::ptr(*buffer), size);

    if (!m_dma_heap_fd_open)
    {
        if (dmabuf_fd_open() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("dmabuf_fd_open failed!");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }

    dma_heap_allocation_data heap_data;
    if (dmabuf_heap_alloc(heap_data, size) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Dma buffer allocation failed on dmabuf_heap_alloc with buffer size = {}", size);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    if (dmabuf_map(heap_data, buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("dmabuf_map failed!");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    if (m_allocated_buffers.find(*buffer) != m_allocated_buffers.end())
    {
        LOGGER__ERROR("DMABUF *buffer already exists in m_allocated_buffers");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    m_allocated_buffers[*buffer] = heap_data;

    fd_count++;
    LOGGER__DEBUG("allocating dma buffer function-end: buffer = {}, size = {}, fd_count = {}", fmt::ptr(*buffer), size,
                  fd_count);

    return MEDIA_LIBRARY_SUCCESS;
}
media_library_return DmaMemoryAllocator::free_dma_buffer(void *buffer)
{
    LOGGER__DEBUG("freeing dma buffer function-start: buffer = {}", fmt::ptr(buffer));

    int fd;
    if (get_fd(buffer, fd, false) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Buffer not found - get_fd failed for buffer = {}", fmt::ptr(buffer));
        return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }

    std::unique_lock<std::mutex> lock(*m_allocator_mutex);

    auto length = m_allocated_buffers[buffer].len;
    m_allocated_buffers.erase(buffer);

    if (munmap(buffer, length) == -1)
    {
        close(fd);
        LOGGER__ERROR("munmap failed!");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    close(fd);

    fd_count--;
    LOGGER__DEBUG("freeing dma buffer function-end: buffer = {}, size = {}, fd_count = {}", fmt::ptr(buffer), length,
                  fd_count);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::dmabuf_sync(int fd, dma_buf_sync &sync)
{
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    int ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);

    if (ret < 0)
    {
        LOGGER__ERROR("ioctl DMA_BUF_IOCTL_SYNC[{}] failed [{}] - fd = {} !", sync.flags, errno, fd);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__DEBUG("dmabuf_sync function-end: fd = {}, start_stop = {}", fd, sync.flags);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::dmabuf_sync(void *buffer, dma_buf_sync &sync)
{
    LOGGER__DEBUG("dmabuf_sync function-start: buffer = {}, start_stop = {}", fmt::ptr(buffer), sync.flags);

    int fd;
    if (get_fd(buffer, fd) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("get_fd failed for buffer = {}", fmt::ptr(buffer));
        return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }

    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    int ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);

    if (ret < 0)
    {
        LOGGER__ERROR("ioctl DMA_BUF_IOCTL_SYNC[{}] failed [{}] - {} !", sync.flags, errno, fmt::ptr(buffer));
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__DEBUG("dmabuf_sync function-end: buffer = {}, start_stop = {}", fmt::ptr(buffer), sync.flags);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::dmabuf_sync_start(int fd)
{
    // Read the cache from device and start the sync
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ,
    };

    return dmabuf_sync(fd, sync);
}

media_library_return DmaMemoryAllocator::dmabuf_sync_start(void *buffer)
{
    // Read the cache from device and start the sync
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ,
    };

    return dmabuf_sync(buffer, sync);
}

media_library_return DmaMemoryAllocator::dmabuf_sync_end(void *buffer)
{
    // Write the cache to the device and finish the sync
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
    };

    return dmabuf_sync(buffer, sync);
}

media_library_return DmaMemoryAllocator::dmabuf_sync_end(int fd)
{
    // Write the cache to the device and finish the sync
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
    };

    return dmabuf_sync(fd, sync);
}

media_library_return DmaMemoryAllocator::get_fd(void *buffer, int &fd, bool include_external)
{
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    LOGGER__DEBUG("get_fd function-start: buffer = {}", fmt::ptr(buffer));

    if (m_allocated_buffers.find(buffer) == m_allocated_buffers.end())
    {
        // TOOD: Change to error once userptr is not supported anymore
        if (include_external && m_external_buffers.find(buffer) != m_external_buffers.end())
        {
            fd = m_external_buffers[buffer].fd;
            return MEDIA_LIBRARY_SUCCESS;
        }
        LOGGER__DEBUG("buffer not found in pre allocated or external buffers (ptr = {})", fmt::ptr(buffer));
        return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
    }

    fd = m_allocated_buffers[buffer].fd;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return DmaMemoryAllocator::get_ptr(uint fd, void **buffer, bool include_external)
{
    std::unique_lock<std::mutex> lock(*m_allocator_mutex);
    LOGGER__DEBUG("get_ptr function-start: fd = {}", fd);

    for (auto const &[key, val] : m_allocated_buffers)
    {
        if (val.fd == fd)
        {
            *buffer = key;
            return MEDIA_LIBRARY_SUCCESS;
        }
    }

    if (include_external)
    {
        for (auto const &[key, val] : m_external_buffers)
        {
            if (val.fd == fd)
            {
                *buffer = key;
                return MEDIA_LIBRARY_SUCCESS;
            }
        }
    }

    LOGGER__DEBUG("buffer not found in allocated or external buffers");
    return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
}
