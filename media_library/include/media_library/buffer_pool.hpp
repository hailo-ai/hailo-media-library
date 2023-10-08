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
 * @file buffer_pool.hpp
 * @brief MediaLibrary BufferPool CPP API module
 **/

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <deque>
#include <memory>
#include <unordered_set>
#include <iostream>
#include <mutex>

#include "dsp_utils.hpp"
#include "v4l2_vsm/hailo_vsm.h"
#include "media_library_types.hpp"

/** @defgroup media_library_buffer_pool_definitions MediaLibrary BufferPool CPP API definitions
 *  @{
 */
enum HailoMemoryType
{
    CMA
};

class MediaLibraryBufferPool;
using MediaLibraryBufferPoolPtr = std::shared_ptr<MediaLibraryBufferPool>;

using DspImagePropertiesPtr = std::shared_ptr<dsp_image_properties_t>;

class HailoBucket;

struct hailo_media_library_buffer;

class HailoBucket
{
private:
    size_t m_buffer_size;
    size_t m_num_buffers;
    HailoMemoryType m_memory_type;

    // Keep track of used and free buffers
    std::unordered_set<intptr_t> m_used_buffers;
    std::deque<intptr_t> m_available_buffers;
    std::shared_ptr<std::mutex> m_bucket_mutex;

    media_library_return allocate();
    media_library_return free();
    media_library_return acquire(intptr_t *buffer_ptr);
    media_library_return release(intptr_t buffer_ptr);
    public:
    HailoBucket(size_t buffer_size, size_t num_buffers, HailoMemoryType memory_type);
    ~HailoBucket();
    // remove copy assigment
    HailoBucket& operator=(const HailoBucket&) = delete;
    // remove copy constructor
    HailoBucket(const HailoBucket&) = delete;
    // remove move constructor
    HailoBucket(HailoBucket&&) = delete;
    //remove move assignment
    HailoBucket& operator=(HailoBucket&&) = delete;
    friend class MediaLibraryBufferPool;    
};
using HailoBucketPtr = std::shared_ptr<HailoBucket>;

class MediaLibraryBufferPool : public std::enable_shared_from_this<MediaLibraryBufferPool>
{
    private:
    std::string m_name;
    std::vector<HailoBucketPtr> m_buckets;
    uint m_width;
    uint m_height;
    dsp_image_format_t m_format;
    public:
    /**
     * @brief Constructor of MediaLibraryBufferPool
     *
     * @param[in] width - buffer width
     * @param[in] height - buffer height
     * @param[in] format - buffer format
     * @param[in] max_buffers - number of buffers to allocate
     * @param[in] memory_type - memory type
     */
    MediaLibraryBufferPool(uint width, uint height, dsp_image_format_t format, size_t max_buffers, HailoMemoryType memory_type);
    ~MediaLibraryBufferPool();
    /**
    * @brief Initialization of MediaLibraryBufferPool
    * Allocates all the required buffers (according to max_buffers)
    *
    * @return media_library_return
    */
    media_library_return init();
    /**
    * @brief Free all the allocated buffers
    * @return media_library_return
    */
    media_library_return free();
    /**
    * @brief Acquire a buffer from the pool
    *
    * @param[out] buffer - hailo_media_library_buffer to the acquire
    * @return media_library_return
    */
    media_library_return acquire_buffer(hailo_media_library_buffer& buffer);
    /**
    * @brief Release a specific plane of a given buffer using the pool
    *
    * @param[out] buffer - hailo_media_library_buffer to the acquire
    * @param[in] plane_index - uint index of the plane to release
    * @return media_library_return
    */
    media_library_return release_plane(hailo_media_library_buffer *buffer, uint32_t plane_index);
    /**
    * @brief Release a buffer from the pool
    *
    * @param[out] buffer - hailo_media_library_buffer to the release
    * @return media_library_return
    */
    media_library_return release_buffer(hailo_media_library_buffer *buffer);
};

struct hailo_media_library_buffer
{
private:
    std::vector<uint> planes_reference_count;
    std::shared_ptr<std::mutex> m_buffer_mutex;
    std::shared_ptr<std::mutex> m_plane_mutex;
    bool has_reference()
    {
        for (uint32_t i = 0; i < planes_reference_count.size(); i++)
        {
            if (planes_reference_count[i] > 0)
                return true;
        }
        return false;
    }

    bool dispose()
    {
        owner = nullptr;
        // free allocated planes memory resources
        delete (hailo_pix_buffer->planes);
        hailo_pix_buffer = nullptr;
        planes_reference_count.clear();
        return true;
    }

    bool release(uint plane_index)
    {
        if (planes_reference_count[plane_index] > 0)
        {
            //log error
            return false;
        }

        if  (owner != nullptr)
        {
            if (owner->release_plane(this, plane_index) != MEDIA_LIBRARY_SUCCESS)
                return false;
        }

        if(!has_reference())
            return dispose();

        return true;
    }
public:
    DspImagePropertiesPtr hailo_pix_buffer;
    MediaLibraryBufferPoolPtr owner;
    hailo15_vsm vsm;

    hailo_media_library_buffer() : m_buffer_mutex(std::make_shared<std::mutex>()), m_plane_mutex(std::make_shared<std::mutex>()), hailo_pix_buffer(nullptr), owner(nullptr) {}

    void *get_plane(uint32_t index)
    {
        if (index >= hailo_pix_buffer->planes_count)
            return nullptr;
        return hailo_pix_buffer->planes[index].userptr;
    }

    uint32_t get_plane_size(uint32_t index)
    {
        if (index >= hailo_pix_buffer->planes_count)
            return 0;
        return hailo_pix_buffer->planes[index].bytesused;
    }

    uint32_t get_num_of_planes()
    {
        return hailo_pix_buffer->planes_count;
    }

    bool increase_ref_count(uint plane_index)
    {
        std::unique_lock<std::mutex> lock(*m_plane_mutex);
        planes_reference_count[plane_index] += 1;
        return true;
    }

    bool increase_ref_count()
    {
        std::unique_lock<std::mutex> lock(*m_buffer_mutex);
        bool ret = true;
        for (uint32_t i = 0; i < planes_reference_count.size(); i++)
            ret = ret && increase_ref_count(i);

        return ret;
    }

    bool decrease_ref_count(uint plane_index)
    {
        std::unique_lock<std::mutex> lock(*m_plane_mutex);
        if (planes_reference_count[plane_index] <= 0)
            return false;

        planes_reference_count[plane_index] -= 1;

        if (planes_reference_count[plane_index] == 0)
            return release(plane_index);

        return true;
    }

    bool decrease_ref_count()
    {
        std::unique_lock<std::mutex> lock(*m_buffer_mutex);
        bool ret = true;
        for (uint32_t i = 0; i < planes_reference_count.size(); i++)
        {
            if(!decrease_ref_count(i))
                ret = false;
        }

        return ret;
    }

    media_library_return create(MediaLibraryBufferPoolPtr owner, DspImagePropertiesPtr hailo_pix_buffer)
    {
        this->owner = owner;
        this->hailo_pix_buffer = hailo_pix_buffer;
        planes_reference_count.reserve(hailo_pix_buffer->planes_count);
        for (uint32_t i = 0; i < hailo_pix_buffer->planes_count; i++) {
            planes_reference_count.emplace_back(0);
        }
        return MEDIA_LIBRARY_SUCCESS;
    }
};
using HailoMediaLibraryBufferPtr = std::shared_ptr<hailo_media_library_buffer>;

static inline bool hailo_media_library_buffer_unref(hailo_media_library_buffer *buffer)
{
    return buffer->decrease_ref_count();
}

static inline bool hailo_media_library_buffer_ref(hailo_media_library_buffer *buffer)
{
    return buffer->increase_ref_count();
}

static inline bool hailo_media_library_plane_unref(std::pair<HailoMediaLibraryBufferPtr, uint> *plane)
{
    HailoMediaLibraryBufferPtr parent_buffer = plane->first;
    uint plane_index = plane->second;
    bool ret = parent_buffer->decrease_ref_count(plane_index);
    delete plane;
    return ret;
}
/** @} */ // end of media_library_buffer_pool_definitions
