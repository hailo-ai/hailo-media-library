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
#include "buffer_pool.hpp"
#include "media_library_logger.hpp"

HailoBucket::HailoBucket(size_t buffer_size, size_t num_buffers,
                         HailoMemoryType memory_type)
    : m_buffer_size(buffer_size), m_num_buffers(num_buffers),
      m_memory_type(memory_type)
{
    m_bucket_mutex = std::make_shared<std::mutex>();
    m_used_buffers.reserve(m_num_buffers);
}

HailoBucket::~HailoBucket() {}

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
        dsp_status result =
            dsp_utils::create_hailo_dsp_buffer(m_buffer_size, &buffer);
        if (result != DSP_SUCCESS)
        {
            LOGGER__ERROR("Failed to create buffer with status code {}",
                          result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        m_available_buffers.push_front((intptr_t)buffer);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::free()
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    if (!m_used_buffers.empty())
    {
        LOGGER__ERROR("There are still {} in the bucket",
                      m_used_buffers.size());
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    while (!m_available_buffers.empty())
    {
        uintptr_t buffer_ptr = m_available_buffers.front();
        dsp_status result =
            dsp_utils::release_hailo_dsp_buffer((void *)buffer_ptr);
        if (result != DSP_SUCCESS)
        {
            LOGGER__ERROR("Failed to release buffer. status code {}", result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        m_available_buffers.pop_front();
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::acquire(intptr_t *buffer_ptr)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    if (m_available_buffers.empty())
    {
        LOGGER__ERROR("Buffer acquire failed - no available buffers remaining, "
                      "please valdiate the max buffers size you set ({})",
                      m_num_buffers);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    *buffer_ptr = m_available_buffers.front();
    m_available_buffers.pop_front();
    m_used_buffers.insert(*buffer_ptr);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::release(intptr_t buffer_ptr)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    // TODO: validate that buffer_ptr is in m_used_buffers?
    m_used_buffers.erase(buffer_ptr);
    m_available_buffers.push_front(buffer_ptr);
    return MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryBufferPool::MediaLibraryBufferPool(uint width, uint height,
                                               dsp_image_format_t format,
                                               size_t max_buffers,
                                               HailoMemoryType memory_type)
    : m_width(width), m_height(height), m_format(format)
{
    m_name = "";
    if (m_name.empty())
        m_name = "pool" + std::to_string(width) + "x" + std::to_string(height) +
                 "_" + std::to_string(max_buffers);

    switch (format)
    {
    case DSP_IMAGE_FORMAT_NV12:
        m_buckets.emplace_back(std::make_shared<HailoBucket>(
            width * height, max_buffers, memory_type));
        m_buckets.emplace_back(std::make_shared<HailoBucket>(
            width * (height / 2), max_buffers, memory_type));
        break;
    case DSP_IMAGE_FORMAT_RGB:
        m_buckets.emplace_back(std::make_shared<HailoBucket>(
            width * height * 3, max_buffers, memory_type));
        break;
    case DSP_IMAGE_FORMAT_GRAY8:
        m_buckets.emplace_back(std::make_shared<HailoBucket>(
            width * height, max_buffers, memory_type));
        break;
    default:
        // TODO: error
        break;
    }
}

MediaLibraryBufferPool::~MediaLibraryBufferPool() { free(); }

media_library_return MediaLibraryBufferPool::free()
{
    for (uint8_t i = 0; i < m_buckets.size(); i++)
    {
        HailoBucketPtr &bucket = m_buckets[i];
        LOGGER__DEBUG("Freeing bucket {} of size {} num of buffers {}", i,
                      bucket->m_buffer_size, bucket->m_num_buffers);
        if (bucket->free() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("failed to free bucket {}", i);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }

    // Release dsp device
    dsp_status status = dsp_utils::release_device();
    if (status != DSP_SUCCESS)
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::init()
{
    // Acquire dsp device
    dsp_status status = dsp_utils::acquire_device();
    if (status != DSP_SUCCESS)
    {
        LOGGER__ERROR("failed to acquire dsp device");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    for (HailoBucketPtr &bucket : m_buckets)
    {
        LOGGER__DEBUG("allocating bucket");
        if (bucket->allocate() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("failed to allocate bucket");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return
MediaLibraryBufferPool::acquire_buffer(hailo_media_library_buffer &buffer)
{
    media_library_return ret = MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    switch (m_format)
    {
    case DSP_IMAGE_FORMAT_NV12:
    {
        size_t y_channel_stride = m_width;
        size_t y_channel_size = y_channel_stride * m_height;
        size_t uv_channel_stride = m_width;
        size_t uv_channel_size = uv_channel_stride * m_height / 2;
        intptr_t y_channel_ptr;

        ret = m_buckets[0]->acquire(&y_channel_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            // GST_ERROR_OBJECT(pool, "Failed to create buffer with status code %d", result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        dsp_data_plane_t y_plane_data = {
            .userptr = (void *)y_channel_ptr,
            .bytesperline = y_channel_stride,
            .bytesused = y_channel_size,
        };

        // Gather uv channel info
        intptr_t uv_channel_ptr;
        ret = m_buckets[1]->acquire(&uv_channel_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            // log Failed to create buffer with status code
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        dsp_data_plane_t uv_plane_data = {
            .userptr = (void *)uv_channel_ptr,
            .bytesperline = uv_channel_stride,
            .bytesused = uv_channel_size,
        };

        LOGGER__DEBUG("Buffers acquired: buffer for y_channel (size = {}), and "
                      "uv_channel (size = {})",
                      y_channel_size, uv_channel_size);

        // TODO: nested struct malloc - free or find a better solution
        dsp_data_plane_t *yuv_planes = new dsp_data_plane_t[2];
        yuv_planes[0] = y_plane_data;
        yuv_planes[1] = uv_plane_data;

        // Fill in dsp_image_properties_t values
        DspImagePropertiesPtr hailo_pix_buffer =
            std::make_shared<dsp_image_properties_t>();
        hailo_pix_buffer->width = m_width;
        hailo_pix_buffer->height = m_height;
        hailo_pix_buffer->planes = yuv_planes;
        hailo_pix_buffer->planes_count = 2;
        hailo_pix_buffer->format = DSP_IMAGE_FORMAT_NV12;

        ret = buffer.create(shared_from_this(), hailo_pix_buffer);
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        buffer.increase_ref_count();
        LOGGER__DEBUG("NV12 Buffer width {} height {} acquired",
                      buffer.hailo_pix_buffer->width,
                      buffer.hailo_pix_buffer->height);
        break;
    }
    case DSP_IMAGE_FORMAT_RGB:
    {
        // TODO: implement
        break;
    }
    case DSP_IMAGE_FORMAT_GRAY8:
    {
        size_t image_stride = m_width;
        size_t image_size = image_stride * m_height;
        intptr_t data_ptr;

        ret = m_buckets[0]->acquire(&data_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        dsp_data_plane_t plane_data = {
            .userptr = (void *)data_ptr,
            .bytesperline = image_stride,
            .bytesused = image_size,
        };

        // TODO: nested struct malloc - free or find a better solution
        dsp_data_plane_t *planes = new dsp_data_plane_t[1];
        planes[0] = plane_data;

        // Fill in dsp_image_properties_t values
        DspImagePropertiesPtr hailo_pix_buffer = std::make_shared<dsp_image_properties_t>();
        hailo_pix_buffer->width = m_width;
        hailo_pix_buffer->height = m_height;
        hailo_pix_buffer->planes = planes;
        hailo_pix_buffer->planes_count = 1;
        hailo_pix_buffer->format = DSP_IMAGE_FORMAT_GRAY8;

        ret = buffer.create(shared_from_this(), hailo_pix_buffer);
        if (ret != MEDIA_LIBRARY_SUCCESS)
            return ret;

        buffer.increase_ref_count();
        LOGGER__DEBUG("GRAY8 Buffer width {} height {} acquired",
                      buffer.hailo_pix_buffer->width,
                      buffer.hailo_pix_buffer->height);
        break;
    }
    default:
    {
        // TODO: error
        break;
    }
    }
    return ret;
}    

media_library_return
MediaLibraryBufferPool::release_plane(hailo_media_library_buffer *buffer,
                                      uint32_t plane_index)
{
    auto bucket = m_buckets[plane_index];
    LOGGER__DEBUG("Releasing plane {} of bucket of size {} num buffers {} used "
                  "buffers {}",
                  plane_index, bucket->m_buffer_size, bucket->m_num_buffers,
                  bucket->m_used_buffers.size() - 1);
    return bucket->release(
        (intptr_t)buffer->hailo_pix_buffer->planes[plane_index].userptr);
}

media_library_return
MediaLibraryBufferPool::release_buffer(hailo_media_library_buffer *buffer)
{
    for (uint32_t i = 0; i < m_buckets.size(); i++)
    {
        if (m_buckets[i]->m_used_buffers.size() > 0)
        {
            media_library_return ret = release_plane(buffer, i);
            if (ret != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__ERROR("failed to release plane number {}", i);
                return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
            }
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}