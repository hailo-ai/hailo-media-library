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
/**
 * @file buffer_pool.hpp
 * @brief MediaLibrary BufferPool CPP API module
 **/

#pragma once
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <unordered_set>
#include <vector>
#include <condition_variable>
#include <functional>

#include "media_library_types.hpp"
#include "hailo_v4l2/hailo_v4l2.h"
#include "dma_memory_allocator.hpp"

/** @defgroup media_library_buffer_pool_definitions MediaLibrary BufferPool CPP
 * API definitions
 *  @{
 */

class MediaLibraryBufferPool;
using MediaLibraryBufferPoolPtr = std::shared_ptr<MediaLibraryBufferPool>;

class HailoBucket;

struct hailo_media_library_buffer;
using HailoMediaLibraryBufferPtr = std::shared_ptr<hailo_media_library_buffer>;

using HailoBucketPtr = std::shared_ptr<HailoBucket>;

/**
 * @class MediaLibraryBufferPool
 * @brief A class representing a buffer pool in the media library.
 *
 * The `MediaLibraryBufferPool` class is responsible for managing a pool of buffers used in the media library.
 * It provides methods for acquiring and releasing buffers, as well as initializing and freeing the pool.
 * The buffer pool can be configured with a specific width, height, format, and maximum number of buffers.
 *
 * Example usage:
 * @code{.cpp}
 * // Create a buffer pool with a width of 640, height of 480, and maximum of 10 buffers
 * MediaLibraryBufferPool bufferPool(640, 480, HAILO_FORMAT_RGB, 10);
 *
 * // Initialize the buffer pool
 * bufferPool.init();
 *
 * // Acquire a buffer from the pool
 * HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();;
 * bufferPool.acquire_buffer(buffer);
 *
 * // Use the acquired buffer
 * // ...
 *
 * // Release the buffer back to the pool
 * bufferPool.release_buffer(&buffer);
 *
 * // Free the buffer pool
 * bufferPool.free();
 * @endcode
 */
class MediaLibraryBufferPool : public std::enable_shared_from_this<MediaLibraryBufferPool>
{
  private:
    std::string m_name;
    std::vector<HailoBucketPtr> m_buckets;
    uint m_width;
    uint m_height;
    uint m_bytes_per_line;
    HailoFormat m_format;
    std::shared_ptr<std::mutex> m_buffer_pool_mutex;
    size_t m_max_buffers;
    uint32_t m_buffer_index;
    std::condition_variable m_pool_cv;

  public:
    /**
     * @brief Constructor of MediaLibraryBufferPool
     *
     * @param[in] width - buffer width
     * @param[in] height - buffer height
     * @param[in] format - buffer format
     * @param[in] max_buffers - number of buffers to allocate
     * @param[in] memory_type - memory type
     * @param[in] name - buffer pool owner name
     */
    MediaLibraryBufferPool(uint width, uint height, HailoFormat format, size_t max_buffers, HailoMemoryType memory_type,
                           std::string name = "");
    /**
     * @brief Constructor of MediaLibraryBufferPool
     *
     * @param[in] width - buffer width
     * @param[in] height - buffer height
     * @param[in] format - buffer format
     * @param[in] max_buffers - number of buffers to allocate
     * @param[in] memory_type - memory type
     * @param[in] bytes_per_line - bytes per line if the buffer stride is padded (when padding=0, bytes_per_line=width)
     * @param[in] name - buffer pool owner name
     */
    MediaLibraryBufferPool(uint width, uint height, HailoFormat format, size_t max_buffers, HailoMemoryType memory_type,
                           uint bytes_per_line, std::string name = "");
    ~MediaLibraryBufferPool();
    // Copy constructor - delete
    MediaLibraryBufferPool(const MediaLibraryBufferPool &) = delete;
    // Copy assignment - delete
    MediaLibraryBufferPool &operator=(const MediaLibraryBufferPool &) = delete;

    int get_available_buffers_count();

    /**
     * @brief Initialization of MediaLibraryBufferPool
     * Allocates all the required buffers (according to max_buffers)
     *
     * @return media_library_return
     *
     * Example usage:
     * @code{.cpp}
     * MediaLibraryBufferPool buffer_pool(width, height, format, max_buffers, memory_type);
     * media_library_return result = buffer_pool.init();
     * if (result != MEDIA_LIBRARY_SUCCESS) {
     *     // Handle error
     * }
     * @endcode
     */
    media_library_return init();
    /**
     * @brief Free all the allocated buffers
     * @param[in] fail_on_used_buffers - bool flag to indicate if the function should fail if there are still used
     * buffers
     * @return media_library_return
     *
     * Example usage:
     * @code{.cpp}
     * media_library_return result = buffer_pool.free();
     * if (result != MEDIA_LIBRARY_SUCCESS) {
     *    // Handle error
     * }
     * @endcode
     */
    media_library_return free(bool fail_on_used_buffers = true);
    /**
     * @brief Acquire a buffer from the pool
     *
     * @param[out] buffer - HailoMediaLibraryBufferPtr to acquire
     * @return media_library_return
     *
     * Example usage:
     * @code{.cpp}
     * HailoMediaLibraryBufferPtr buffer;
     * media_library_return result = buffer_pool.acquire_buffer(buffer);
     * if (result != MEDIA_LIBRARY_SUCCESS) {
     *     // Handle error
     * }
     * @endcode
     */
    media_library_return acquire_buffer(HailoMediaLibraryBufferPtr buffer);
    /**
     * @brief Release a specific plane of a given buffer using the pool
     *
     * @param[out] buffer - pointer to hailo_media_library_buffer that holds the plane
     * @param[in] plane_index - uint index of the plane to release
     * @return media_library_return
     */
    media_library_return release_plane(hailo_media_library_buffer *buffer, uint32_t plane_index);
    /**
     * @brief Release a buffer back to the pool
     *
     * @param[out] buffer - HailoMediaLibraryBufferPtr to release
     * @return media_library_return
     *
     * Example usage:
     * @code{.cpp}
     * media_library_return result = buffer_pool.release_buffer(buffer);
     * if (result != MEDIA_LIBRARY_SUCCESS) {
     *    // Handle error
     * }
     * @endcode
     */
    media_library_return release_buffer(HailoMediaLibraryBufferPtr buffer);

    /**
     * @brief Applies a given function to all the buffers in the buffer pool.
     *
     * This function iterates over all the available and used buffers in the buffer pool and applies
     * the given function to each buffer. a common use of this function would be to map and unmap the buffers to a
     * device.
     *
     * @param func The function to apply to each buffer, the function takes two parameters: the file descriptor (fd)
     * associated with the buffer and the buffer size.
     * @return media_library_return Returns MEDIA_LIBRARY_SUCCESS if the function was successfully
     * applied to all buffers. Otherwise, it returns MEDIA_LIBRARY_ERROR.
     */
    media_library_return for_each_buffer(std::function<bool(int, size_t)> func);

    /**
     * @brief Waits for all the buffers in the pool to be used.
     * @param timeout_ms The timeout in milliseconds to wait for the buffers to be used.
     *
     * @return media_library_return Returns MEDIA_LIBRARY_SUCCESS if all the buffers are used within the timeout.
     */
    media_library_return wait_for_used_buffers(
        const std::chrono::milliseconds &timeout_ms = std::chrono::milliseconds(1000));

    /**
     * @brief Swaps the width and height of the buffer.
     * @return The return status of the swap operation.
     */
    media_library_return swap_width_and_height();
    /**
     * @brief Gets the width of the buffer pool.
     *
     * @return The width of the buffer pool as an unsigned integer.
     */
    uint get_width()
    {
        return m_width;
    }
    /**
     * @brief Gets the height of the buffer pool.
     *
     * @return The height of the buffer pool as an unsigned integer.
     */
    uint get_height()
    {
        return m_height;
    }
    /**
     * @brief Gets the max size of the buffer pool.
     *
     * @return The height of the buffer pool as an unsigned integer.
     */
    uint get_size()
    {
        return m_max_buffers;
    }

    /**
     * @brief Gets the name of the buffer pool.
     *
     * @return The name of the buffer pool as a string.
     */
    std::string get_name()
    {
        return m_name;
    }

    /**
     * @brief Gets the format of the buffer pool.
     *
     * @return The format of the buffer pool as a HailoFormat enum.
     */
    HailoFormat get_format()
    {
        return m_format;
    }
};

struct hailo_media_library_buffer
{
  private:
    std::shared_ptr<std::mutex> m_buffer_mutex;
    std::shared_ptr<std::mutex> m_plane_mutex;

    bool dispose()
    {
        owner = nullptr;
        buffer_data = nullptr;
        return true;
    }
    std::function<void(void *)> on_free;
    void *on_free_data;

  public:
    HailoBufferDataPtr buffer_data;
    MediaLibraryBufferPoolPtr owner;
    struct hailo15_vsm vsm;
    int32_t isp_ae_fps;
    bool isp_ae_converged;
    uint64_t isp_ae_integration_time;
    uint8_t isp_ae_average_luma;
    int32_t video_fd;
    uint32_t buffer_index;
    uint64_t isp_timestamp_ns;
    uint64_t pts;
    HailoMediaLibraryBufferPtr motion_detection_buffer;
    bool motion_detected;

    hailo_media_library_buffer()
        : m_buffer_mutex(std::make_shared<std::mutex>()), m_plane_mutex(std::make_shared<std::mutex>()),
          buffer_data(nullptr), owner(nullptr), isp_ae_fps(HAILO_ISP_AE_FPS_DEFAULT_VALUE),
          isp_ae_converged(HAILO_ISP_AE_CONVERGED_DEFAULT_VALUE),
          isp_ae_integration_time(HAILO_ISP_AE_INTEGRATION_TIME_DEFAULT_VALUE),
          isp_ae_average_luma(HAILO_ISP_AE_LUMA_DEFUALT_VALUE), video_fd(-1), buffer_index(0), isp_timestamp_ns(0),
          pts(0), motion_detection_buffer(nullptr), motion_detected(false)
    {
        vsm.dx = HAILO_VSM_DEFAULT_VALUE;
        vsm.dy = HAILO_VSM_DEFAULT_VALUE;
    }

    ~hailo_media_library_buffer()
    {
        // free the planes back to their buffer pools
        if (buffer_data)
        {
            if (owner != nullptr)
            {
                for (uint plane_index = 0; plane_index < buffer_data->planes_count; plane_index++)
                {
                    owner->release_plane(this, plane_index);
                }
            }
            else
            {
                // External buffer, unmap from dma memory allocator if exists
                for (uint plane_index = 0; plane_index < buffer_data->planes_count; plane_index++)
                {
                    DmaMemoryAllocator::get_instance().unmap_external_dma_buffer(get_plane_ptr(plane_index));
                }
            }
        }

        if (on_free)
            on_free(on_free_data);

        dispose();
    }
    // Move constructor
    hailo_media_library_buffer(hailo_media_library_buffer &&other) noexcept
    {
        m_buffer_mutex = other.m_buffer_mutex;
        m_plane_mutex = other.m_plane_mutex;
        buffer_data = other.buffer_data;
        owner = other.owner;
        vsm = other.vsm;
        isp_ae_fps = other.isp_ae_fps;
        isp_ae_converged = other.isp_ae_converged;
        isp_ae_integration_time = other.isp_ae_integration_time;
        isp_ae_average_luma = other.isp_ae_average_luma;
        isp_timestamp_ns = other.isp_timestamp_ns;
        video_fd = other.video_fd;
        buffer_index = other.buffer_index;
        pts = other.pts;
        motion_detection_buffer = other.motion_detection_buffer;
        motion_detected = other.motion_detected;
        on_free = other.on_free;
        on_free_data = other.on_free_data;
        other.buffer_data = nullptr;
        other.owner = nullptr;
        other.m_buffer_mutex = nullptr;
        other.m_plane_mutex = nullptr;
        other.isp_ae_fps = HAILO_ISP_AE_FPS_DEFAULT_VALUE;
        other.isp_ae_converged = HAILO_ISP_AE_CONVERGED_DEFAULT_VALUE;
        other.isp_ae_integration_time = HAILO_ISP_AE_INTEGRATION_TIME_DEFAULT_VALUE;
        other.isp_ae_average_luma = HAILO_ISP_AE_LUMA_DEFUALT_VALUE;
        other.isp_timestamp_ns = 0;
        other.video_fd = -1;
        other.vsm.dx = HAILO_VSM_DEFAULT_VALUE;
        other.vsm.dy = HAILO_VSM_DEFAULT_VALUE;
        other.buffer_index = 0;
        other.pts = 0;
        other.motion_detection_buffer = nullptr;
        other.motion_detected = false;
        other.on_free = nullptr;
        other.on_free_data = nullptr;
    }

    // Move assignment
    hailo_media_library_buffer &operator=(hailo_media_library_buffer &&other) noexcept
    {
        if (this != &other)
        {
            m_buffer_mutex = other.m_buffer_mutex;
            m_plane_mutex = other.m_plane_mutex;
            buffer_data = other.buffer_data;
            owner = other.owner;
            vsm = other.vsm;
            isp_ae_fps = other.isp_ae_fps;
            isp_ae_converged = other.isp_ae_converged;
            isp_ae_integration_time = other.isp_ae_integration_time;
            isp_ae_average_luma = other.isp_ae_average_luma;
            isp_timestamp_ns = other.isp_timestamp_ns;
            video_fd = other.video_fd;
            buffer_index = other.buffer_index;
            pts = other.pts;
            motion_detection_buffer = other.motion_detection_buffer;
            motion_detected = other.motion_detected;
            on_free = other.on_free;
            on_free_data = other.on_free_data;
            other.buffer_data = nullptr;
            other.owner = nullptr;
            other.m_buffer_mutex = nullptr;
            other.m_plane_mutex = nullptr;
            other.isp_ae_fps = HAILO_ISP_AE_FPS_DEFAULT_VALUE;
            other.isp_ae_converged = HAILO_ISP_AE_CONVERGED_DEFAULT_VALUE;
            other.isp_ae_integration_time = HAILO_ISP_AE_INTEGRATION_TIME_DEFAULT_VALUE;
            other.isp_ae_average_luma = HAILO_ISP_AE_LUMA_DEFUALT_VALUE;
            other.isp_timestamp_ns = 0;
            other.video_fd = -1;
            other.vsm.dx = HAILO_VSM_DEFAULT_VALUE;
            other.vsm.dy = HAILO_VSM_DEFAULT_VALUE;
            other.buffer_index = 0;
            other.pts = 0;
            other.motion_detection_buffer = nullptr;
            other.motion_detected = false;
            other.on_free = nullptr;
            other.on_free_data = nullptr;
        }
        return *this;
    }

    // Copy constructor - delete
    hailo_media_library_buffer(const hailo_media_library_buffer &other) = delete;
    // Copy assignment - delete
    hailo_media_library_buffer &operator=(const hailo_media_library_buffer &other) = delete;

    void copy_metadata_from(const HailoMediaLibraryBufferPtr &other)
    {
        if (other == nullptr)
        {
            return;
        }

        isp_ae_fps = other->isp_ae_fps;
        isp_ae_converged = other->isp_ae_converged;
        isp_ae_integration_time = other->isp_ae_integration_time;
        isp_ae_average_luma = other->isp_ae_average_luma;
        isp_timestamp_ns = other->isp_timestamp_ns;
        video_fd = other->video_fd;
        buffer_index = other->buffer_index;
        pts = other->pts;
        motion_detection_buffer = other->motion_detection_buffer;
        motion_detected = other->motion_detected;
    }

    void *get_plane_ptr(uint32_t index)
    {
        if (index >= buffer_data->planes_count)
            return nullptr;

        return buffer_data->planes[index].userptr;
    }

    int get_plane_fd(uint32_t index)
    {
        if (index >= buffer_data->planes_count)
            return -1;
        return buffer_data->planes[index].fd;
    }

    uint32_t get_plane_size(uint32_t index)
    {
        if (index >= buffer_data->planes_count)
            return 0;
        return buffer_data->planes[index].bytesused;
    }

    uint32_t get_plane_stride(uint32_t index)
    {
        if (index >= buffer_data->planes_count)
            return 0;
        return buffer_data->planes[index].bytesperline;
    }

    uint32_t get_num_of_planes()
    {
        return buffer_data->planes_count;
    }

    void set_buffer_index(uint32_t buffer_index)
    {
        this->buffer_index = buffer_index;
    }

    media_library_return create(MediaLibraryBufferPoolPtr owner, HailoBufferDataPtr buffer_data,
                                std::function<void(void *)> on_free = nullptr, void *on_free_data = nullptr)
    {
        this->owner = owner;
        this->buffer_data = buffer_data;
        this->on_free = on_free;
        this->on_free_data = on_free_data;
        return MEDIA_LIBRARY_SUCCESS;
    }

    bool is_dmabuf()
    {
        return (buffer_data->memory == HAILO_MEMORY_TYPE_DMABUF);
    }

    media_library_return sync_start(uint plane)
    {
        if (!is_dmabuf())
        {
            return MEDIA_LIBRARY_ERROR;
        }

        int plane_fd = get_plane_fd(plane);
        if (plane_fd == -1)
            return MEDIA_LIBRARY_ERROR;

        media_library_return ret = DmaMemoryAllocator::get_instance().dmabuf_sync_start(plane_fd);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            return ret;
        }

        return MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return sync_start()
    {
        if (!is_dmabuf())
        {
            return MEDIA_LIBRARY_ERROR;
        }

        for (uint32_t i = 0; i < get_num_of_planes(); i++)
        {
            if (sync_start(i) != MEDIA_LIBRARY_SUCCESS)
            {
                return MEDIA_LIBRARY_ERROR;
            }
        }

        return MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return sync_end(uint plane)
    {
        int plane_fd = get_plane_fd(plane);
        if (plane_fd == -1)
            return MEDIA_LIBRARY_ERROR;

        media_library_return ret = DmaMemoryAllocator::get_instance().dmabuf_sync_end(plane_fd);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            return ret;
        }

        return MEDIA_LIBRARY_SUCCESS;
    }

    media_library_return sync_end()
    {
        if (!is_dmabuf())
        {
            return MEDIA_LIBRARY_ERROR;
        }

        for (uint32_t i = 0; i < get_num_of_planes(); i++)
        {
            media_library_return ret = sync_end(i);
            if (ret != MEDIA_LIBRARY_SUCCESS)
            {
                return ret;
            }
        }

        return MEDIA_LIBRARY_SUCCESS;
    }

    void *get_on_free_data()
    {
        return on_free_data;
    }
};

/** @} */ // end of media_library_buffer_pool_definitions
