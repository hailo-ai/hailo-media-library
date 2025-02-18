/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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
#include <thread>
#include "buffer_pool.hpp"
#include "media_library_logger.hpp"

HailoBucket::HailoBucket(size_t buffer_size, size_t num_buffers, HailoMemoryType memory_type)
    : m_buffer_size(buffer_size), m_num_buffers(num_buffers), m_memory_type(memory_type)
{
    m_bucket_mutex = std::make_shared<std::mutex>();
    m_used_buffers.reserve(m_num_buffers);
}

HailoBucket::~HailoBucket()
{
}

media_library_return HailoBucket::allocate()
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    if (m_available_buffers.size() >= m_num_buffers)
    {
        LOGGER__ERROR("Exeeded max buffers");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    size_t buffers_to_allocate = m_num_buffers - m_available_buffers.size();

    for (size_t i = 0; i < buffers_to_allocate; i++)
    {
        void *buffer = NULL;
        media_library_return result = DmaMemoryAllocator::get_instance().allocate_dma_buffer(m_buffer_size, &buffer);

        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to create buffer with status code {}", result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        m_available_buffers.push_front((intptr_t)buffer);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::free(bool fail_on_used_buffers)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);

    bool used_buffers_exist = !m_used_buffers.empty();
    if (used_buffers_exist)
    {
        LOGGER__ERROR("There are still {} used buffers in the bucket, {} are free", m_used_buffers.size(),
                      m_available_buffers.size());
        for (intptr_t buffer_ptr : m_used_buffers)
        {
            LOGGER__INFO("Freeing bucket: buffer {} still used", (void *)buffer_ptr);
            if (!fail_on_used_buffers)
                m_available_buffers.push_front(buffer_ptr);
        }
        if (!fail_on_used_buffers)
            m_used_buffers.clear();
    }

    while (!m_available_buffers.empty())
    {
        intptr_t buffer_ptr = m_available_buffers.front();
        media_library_return result =
            DmaMemoryAllocator::get_instance().free_dma_buffer(reinterpret_cast<void *>(buffer_ptr));

        if (result != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to release buffer. status code {}", result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        m_available_buffers.pop_front();
    }

    if (fail_on_used_buffers && used_buffers_exist)
    {
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__DEBUG("After freeing bucket of size {} num of buffers {}, used buffers {} available buffers {}",
                  m_buffer_size, m_num_buffers, m_used_buffers.size(), m_available_buffers.size());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::acquire(intptr_t *buffer_ptr)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);

    if (m_available_buffers.empty())
    {
        LOGGER__ERROR("Buffer acquire failed - no available buffers remaining, "
                      "please validate the max buffers size you set ({})",
                      m_num_buffers);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    *buffer_ptr = m_available_buffers.front();
    m_available_buffers.pop_front();
    m_used_buffers.insert(*buffer_ptr);

    LOGGER__DEBUG("After acquiring buffer {}, available_buffers={} used_buffers={}", *buffer_ptr,
                  m_available_buffers.size(), m_used_buffers.size());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::release(intptr_t buffer_ptr)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);

    auto num_erased = m_used_buffers.erase(buffer_ptr);
    if (!(num_erased == 0))
    {
        m_available_buffers.push_front(buffer_ptr);
    }

    LOGGER__DEBUG("After release buffer {}, total_buffers={}  available_buffers={} used_buffers={}, removed={}",
                  buffer_ptr, m_num_buffers, m_available_buffers.size(), m_used_buffers.size(), num_erased);

    return MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryBufferPool::MediaLibraryBufferPool(uint width, uint height, HailoFormat format, size_t max_buffers,
                                               HailoMemoryType memory_type, uint bytes_per_line, std::string owner_name)
    : m_width(width), m_height(height), m_bytes_per_line(bytes_per_line), m_format(format), m_max_buffers(max_buffers)
{
    m_buffer_index = 0;
    m_name = "";
    if (m_name.empty())
    {
        if (owner_name.empty())
            m_name =
                "pool" + std::to_string(width) + "x" + std::to_string(height) + "_" + std::to_string(m_max_buffers);
        else
            m_name = owner_name + " pool" + std::to_string(width) + "x" + std::to_string(height) + "_" +
                     std::to_string(m_max_buffers);
    }

    LOGGER__INFO("Creating buffer pool with name {}", m_name);

    m_buffer_pool_mutex = std::make_shared<std::mutex>();

    switch (format)
    {
    case HAILO_FORMAT_NV12:
        m_buckets.emplace_back(std::make_shared<HailoBucket>(bytes_per_line * height, max_buffers, memory_type));
        m_buckets.emplace_back(std::make_shared<HailoBucket>(bytes_per_line * (height / 2), max_buffers, memory_type));
        break;
    case HAILO_FORMAT_RGB:
        m_buckets.emplace_back(std::make_shared<HailoBucket>(bytes_per_line * height * 3, max_buffers, memory_type));
        break;
    case HAILO_FORMAT_GRAY8:
        m_buckets.emplace_back(std::make_shared<HailoBucket>(bytes_per_line * height, max_buffers, memory_type));
        break;
    default:
        // TODO: error
        break;
    }
}

MediaLibraryBufferPool::MediaLibraryBufferPool(uint width, uint height, HailoFormat format, size_t max_buffers,
                                               HailoMemoryType memory_type, std::string owner_name)
    : MediaLibraryBufferPool(width, height, format, max_buffers, memory_type, width, owner_name)
{
}

MediaLibraryBufferPool::~MediaLibraryBufferPool()
{
    LOGGER__INFO("Destroying buffer pool with name {}", m_name);
    free();
}

media_library_return MediaLibraryBufferPool::wait_for_used_buffers(uint timeout_in_ms)
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
    for (uint8_t i = 0; i < m_buckets.size(); i++)
    {
        HailoBucketPtr &bucket = m_buckets[i];
        LOGGER__DEBUG("{}: Waiting for bucket {} of size {} num of buffers {}", m_name, i, bucket->m_buffer_size,
                      bucket->m_num_buffers);

        if (!m_pool_cv.wait_for(lock, std::chrono::milliseconds(timeout_in_ms),
                                [&bucket]() { return bucket->used_buffers_count() == 0; }))
        {
            LOGGER__ERROR("{}: Timeout waiting for used buffers to be released", m_name);
            return MEDIA_LIBRARY_ERROR;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::free(bool fail_on_used_buffers)
{
    for (uint8_t i = 0; i < m_buckets.size(); i++)
    {
        HailoBucketPtr &bucket = m_buckets[i];
        LOGGER__DEBUG("{}: Freeing bucket {} of size {} num of buffers {}", m_name, i, bucket->m_buffer_size,
                      bucket->m_num_buffers);
        if (bucket->free(fail_on_used_buffers) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("{}: failed to free bucket {}", m_name, i);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::init()
{
    for (HailoBucketPtr &bucket : m_buckets)
    {
        LOGGER__DEBUG("{}: allocating bucket of size {} num of buffers {}", m_name, bucket->m_buffer_size,
                      bucket->m_num_buffers);
        if (bucket->allocate() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("{}: failed to allocate bucket", m_name);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::for_each_buffer(std::function<bool(int, size_t)> func)
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);

    for (HailoBucketPtr &bucket : m_buckets)
    {
        for (intptr_t buffer_ptr : bucket->m_available_buffers)
        {
            int fd;
            if (DmaMemoryAllocator::get_instance().get_fd((void *)buffer_ptr, fd) != MEDIA_LIBRARY_SUCCESS)
            {
                return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
            }

            if (!func(fd, bucket->m_buffer_size))
            {
                return MEDIA_LIBRARY_ERROR;
            }
        }

        for (intptr_t buffer_ptr : bucket->m_used_buffers)
        {
            int fd;
            if (DmaMemoryAllocator::get_instance().get_fd((void *)buffer_ptr, fd) != MEDIA_LIBRARY_SUCCESS)
            {
                return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
            }

            if (!func(fd, bucket->m_buffer_size))
            {
                return MEDIA_LIBRARY_ERROR;
            }
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::swap_width_and_height()
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);

    uint temp = m_width;
    m_width = m_height;
    m_height = temp;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::acquire_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
    m_buffer_index++;
    if (m_buffer_index > m_max_buffers)
        m_buffer_index = 1;
    LOGGER__DEBUG("{}: Acquiring buffer number {}", m_name, m_buffer_index);
    media_library_return ret = MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    switch (m_format)
    {
    case HAILO_FORMAT_NV12: {
        size_t y_channel_stride = m_bytes_per_line;
        size_t y_channel_size = y_channel_stride * m_height;
        size_t uv_channel_stride = m_bytes_per_line;
        size_t uv_channel_size = uv_channel_stride * m_height / 2;
        intptr_t y_channel_ptr;

        ret = m_buckets[0]->acquire(&y_channel_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            // GST_ERROR_OBJECT(pool, "Failed to create buffer with status code %d", result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        hailo_data_plane_t y_plane_data;
        y_plane_data.bytesperline = y_channel_stride;
        y_plane_data.bytesused = y_channel_size;

        int y_channel_fd;
        ret = DmaMemoryAllocator::get_instance().get_fd((void *)y_channel_ptr, y_channel_fd);

        y_plane_data.userptr = (void *)y_channel_ptr;
        if (ret == MEDIA_LIBRARY_SUCCESS)
        {
            y_plane_data.fd = y_channel_fd;
        }
        else
        {
            LOGGER__ERROR("CMA memory not supported");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        // Gather uv channel info
        intptr_t uv_channel_ptr;
        ret = m_buckets[1]->acquire(&uv_channel_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            // log Failed to create buffer with status code
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        hailo_data_plane_t uv_plane_data;
        uv_plane_data.bytesperline = uv_channel_stride;
        uv_plane_data.bytesused = uv_channel_size;

        int uv_channel_fd;
        ret = DmaMemoryAllocator::get_instance().get_fd((void *)uv_channel_ptr, uv_channel_fd);

        HailoMemoryType memory_type;
        uv_plane_data.userptr = (void *)uv_channel_ptr;
        if (ret == MEDIA_LIBRARY_SUCCESS)
        {
            uv_plane_data.fd = uv_channel_fd;
            memory_type = HAILO_MEMORY_TYPE_DMABUF;
        }
        else
        {
            memory_type = HAILO_MEMORY_TYPE_CMA;
            LOGGER__ERROR("CMA memory not supported");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        LOGGER__DEBUG("{}: Buffers acquired: buffer for y_channel (size = {}), and "
                      "uv_channel (size = {})",
                      m_name, y_channel_size, uv_channel_size);

        // Fill in buffer_data values
        HailoBufferDataPtr buffer_data = std::make_shared<hailo_buffer_data_t>(
            (size_t)m_width, (size_t)m_height, (size_t)2, HAILO_FORMAT_NV12, memory_type,
            std::vector<hailo_data_plane_t>{y_plane_data, uv_plane_data});

        ret = buffer->create(shared_from_this(), buffer_data);
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;
        buffer->set_buffer_index(m_buffer_index);
        LOGGER__DEBUG("{}: NV12 Buffer width {} height {} acquired", m_name, buffer->buffer_data->width,
                      buffer->buffer_data->height);
        break;
    }
    case HAILO_FORMAT_RGB: {
        // TODO: implement
        break;
    }
    case HAILO_FORMAT_GRAY8: {
        size_t image_stride = m_bytes_per_line;
        size_t image_size = image_stride * m_height;
        intptr_t data_ptr;

        ret = m_buckets[0]->acquire(&data_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        hailo_data_plane_t plane_data;
        plane_data.bytesperline = image_stride;
        plane_data.bytesused = image_size;

        int channel_fd;
        ret = DmaMemoryAllocator::get_instance().get_fd((void *)data_ptr, channel_fd);

        HailoMemoryType memory_type = HAILO_MEMORY_TYPE_DMABUF;
        plane_data.userptr = (void *)data_ptr;
        if (ret == MEDIA_LIBRARY_SUCCESS)
        {
            plane_data.fd = channel_fd;
            memory_type = HAILO_MEMORY_TYPE_DMABUF;
        }
        else
        {
            memory_type = HAILO_MEMORY_TYPE_CMA;
            LOGGER__ERROR("CMA memory not supported");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        // Fill in buffer_data values
        HailoBufferDataPtr buffer_data =
            std::make_shared<hailo_buffer_data_t>((size_t)m_width, (size_t)m_height, (size_t)1, HAILO_FORMAT_GRAY8,
                                                  memory_type, std::vector<hailo_data_plane_t>{plane_data});

        ret = buffer->create(shared_from_this(), buffer_data);
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        LOGGER__DEBUG("{}: GRAY8 Buffer width {} height {} acquired", m_name, buffer->buffer_data->width,
                      buffer->buffer_data->height);
        break;
    }
    default: {
        // TODO: error
        break;
    }
    }
    return ret;
}

int HailoBucket::available_buffers_count()
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    return m_available_buffers.size();
}

int HailoBucket::used_buffers_count()
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    return m_used_buffers.size();
}

int MediaLibraryBufferPool::get_available_buffers_count()
{
    return m_buckets[0]->available_buffers_count();
}

media_library_return MediaLibraryBufferPool::release_plane(hailo_media_library_buffer *buffer, uint32_t plane_index)
{
    auto bucket = m_buckets[plane_index];
    LOGGER__DEBUG("{}: Releasing plane {} of buffer with index {} of bucket of size {} num buffers {} used buffers {}",
                  m_name, plane_index, buffer->buffer_index, bucket->m_buffer_size, bucket->m_num_buffers,
                  bucket->m_used_buffers.size() - 1);

    if (buffer->is_dmabuf())
    {
        return bucket->release((intptr_t)buffer->get_plane_ptr(plane_index));
    }

    return bucket->release((intptr_t)buffer->buffer_data->planes[plane_index].userptr);
}

media_library_return MediaLibraryBufferPool::release_buffer(HailoMediaLibraryBufferPtr buffer)
{
    for (uint32_t i = 0; i < m_buckets.size(); i++)
    {
        if (m_buckets[i]->m_used_buffers.size() > 0)
        {
            media_library_return ret = release_plane(buffer.get(), i);
            if (ret != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__ERROR("{}: failed to release plane number {}", m_name, i);
                return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
            }
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}
