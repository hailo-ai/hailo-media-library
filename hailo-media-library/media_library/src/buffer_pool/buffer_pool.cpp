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
#include <stdint.h>
#include <sys/types.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iterator>
#include <limits>
#include <map>
#include <type_traits>
#include <variant>

#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "dma_memory_allocator.hpp"
#include "hailo_media_library_perfetto.hpp"
#include "media_library_buffer.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "dsp_utils.hpp"
#include "encoder_config_types.hpp"

#define MODULE_NAME LoggerType::BufferPool

// -- Format size multipliers --
static constexpr size_t NV12_UV_HEIGHT_DIVISOR = 2;
static constexpr size_t RGB_BYTES_PER_PIXEL = 3;
static constexpr double GRAY12_BYTES_PER_PIXEL = 1.5;
static constexpr size_t GRAY16_BYTES_PER_PIXEL = 2;

// Compute per-bucket buffer sizes for a given format and geometry.
// NV12 produces two buckets (Y + UV), all other formats produce one.
static std::vector<size_t> compute_bucket_sizes(size_t bytes_per_line, uint height, HailoFormat format)
{
    std::vector<size_t> sizes;
    switch (format)
    {
    case HAILO_FORMAT_NV12:
        sizes.push_back(bytes_per_line * height);
        sizes.push_back(bytes_per_line * (height / NV12_UV_HEIGHT_DIVISOR));
        break;
    case HAILO_FORMAT_RGB:
        sizes.push_back(bytes_per_line * height * RGB_BYTES_PER_PIXEL);
        break;
    case HAILO_FORMAT_GRAY8:
        sizes.push_back(bytes_per_line * height);
        break;
    case HAILO_FORMAT_GRAY12:
        sizes.push_back(static_cast<size_t>(bytes_per_line * height * GRAY12_BYTES_PER_PIXEL));
        break;
    case HAILO_FORMAT_GRAY16:
        sizes.push_back(bytes_per_line * height * GRAY16_BYTES_PER_PIXEL);
        break;
    default:
        break;
    }
    return sizes;
}

// -- Pre-allocation cache (file-local) --
// Kept as file-local statics rather than class statics because the free function
// try_acquire_preallocated is called from HailoBucket (a .cpp-local class).
// Making these class members would force try_acquire_preallocated into the public
// API just to satisfy HailoBucket's access, leaking an implementation detail.
static std::mutex s_prealloc_mutex;
static std::unordered_map<size_t, std::deque<intptr_t>> s_prealloc_buffers;

// Background thread launched by preallocate_from_config and the cancel flag it
// polls between items. Tracked (not detached) so clear_prealloc_cache can wait
// for it to exit before freeing the buffer set.
static std::thread s_prealloc_thread;
static std::atomic<bool> s_prealloc_cancel{false};

static bool try_acquire_preallocated(size_t buffer_size, void **buffer)
{
    std::lock_guard<std::mutex> lock(s_prealloc_mutex);
    auto it = s_prealloc_buffers.find(buffer_size);
    if (it == s_prealloc_buffers.end() || it->second.empty())
        return false;
    *buffer = reinterpret_cast<void *>(it->second.front());
    it->second.pop_front();
    LOGGER__MODULE__INFO(MODULE_NAME, "Pre-alloc cache: acquired buffer of size {} ({} remaining)", buffer_size,
                         it->second.size());
    return true;
}

class HailoBucket
{
  private:
    size_t m_buffer_size;
    size_t m_num_buffers;
    HailoMemoryType m_memory_type;
    std::string m_name;
#ifdef HAVE_PERFETTO
    perfetto::CounterTrack m_counter_track;
#endif
    // Keep track of used and free buffers
    std::unordered_set<intptr_t> m_used_buffers;
    std::deque<intptr_t> m_available_buffers;
    std::shared_ptr<std::mutex> m_bucket_mutex;
    std::condition_variable m_bucket_cv;

    media_library_return allocate_chunk(size_t count, bool use_prealloc = false);
    media_library_return free(bool fail_on_used_buffers = true);
    media_library_return acquire(intptr_t *buffer_ptr);
    media_library_return release(intptr_t buffer_ptr);

  public:
    HailoBucket(size_t buffer_size, size_t num_buffers, HailoMemoryType memory_type, std::string name);
    ~HailoBucket();
    // remove copy assigment
    HailoBucket &operator=(const HailoBucket &) = delete;
    // remove copy constructor
    HailoBucket(const HailoBucket &) = delete;
    // remove move constructor
    HailoBucket(HailoBucket &&) = delete;
    // remove move assignment
    HailoBucket &operator=(HailoBucket &&) = delete;
    friend class MediaLibraryBufferPool;
    int available_buffers_count();
    int used_buffers_count();
    media_library_return wait_for_used_buffers(const std::chrono::milliseconds &timeout_ms);
};

HailoBucket::HailoBucket(size_t buffer_size, size_t num_buffers, HailoMemoryType memory_type, std::string name)
    : m_buffer_size(buffer_size), m_num_buffers(num_buffers), m_memory_type(memory_type), m_name(name)
#ifdef HAVE_PERFETTO
      ,
      m_counter_track(perfetto::DynamicString(m_name), BUFFER_POOLS_TRACK)
#endif
{
    m_bucket_mutex = std::make_shared<std::mutex>();
    m_used_buffers.reserve(m_num_buffers);
}

HailoBucket::~HailoBucket()
{
}

media_library_return HailoBucket::allocate_chunk(size_t count, bool use_prealloc)
{
    size_t to_allocate;
    {
        std::unique_lock<std::mutex> lock(*m_bucket_mutex);

        // Cap to remaining capacity
        size_t current = m_available_buffers.size() + m_used_buffers.size();
        if (current >= m_num_buffers)
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: allocate_chunk skipped - already fully allocated ({}/{})", m_name,
                                  current, m_num_buffers);
            return MEDIA_LIBRARY_SUCCESS;
        }
        size_t remaining = m_num_buffers - current;
        to_allocate = std::min(count, remaining);
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: allocate_chunk: allocating {} buffers (requested={})", m_name, to_allocate,
                          count);

    // Allocate DMA buffers without holding the bucket mutex so that
    // concurrent acquire/available_buffers_count calls are not blocked.
    std::vector<intptr_t> tmp_allocated_buffers;
    for (size_t i = 0; i < to_allocate; i++)
    {
        void *buffer = nullptr;
        bool from_prealloc = use_prealloc && try_acquire_preallocated(m_buffer_size, &buffer);
        if (!from_prealloc && use_prealloc)
        {
            LOGGER__MODULE__WARN(MODULE_NAME, "{}: Pre-alloc cache miss for buffer_size={}", m_name, m_buffer_size);
        }
        if (!from_prealloc)
        {
            media_library_return result =
                DmaMemoryAllocator::get_instance().allocate_dma_buffer(m_buffer_size, &buffer);
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                for (intptr_t buf : tmp_allocated_buffers)
                {
                    media_library_return free_result =
                        DmaMemoryAllocator::get_instance().free_dma_buffer(reinterpret_cast<void *>(buf));
                    if (free_result != MEDIA_LIBRARY_SUCCESS)
                    {
                        LOGGER__MODULE__ERROR(MODULE_NAME,
                                              "{}: Failed to release buffer during rollback, status code {}", m_name,
                                              free_result);
                    }
                }
                LOGGER__MODULE__ERROR(MODULE_NAME, "{}: Failed to allocate chunk buffer with status code {}", m_name,
                                      result);
                return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
            }
        }
        tmp_allocated_buffers.push_back(reinterpret_cast<intptr_t>(buffer));
    }

    {
        std::unique_lock<std::mutex> lock(*m_bucket_mutex);
        for (intptr_t buffer : tmp_allocated_buffers)
        {
            m_available_buffers.push_front(buffer);
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: allocate_chunk done - available={} used={} total={}", m_name,
                              m_available_buffers.size(), m_used_buffers.size(),
                              m_available_buffers.size() + m_used_buffers.size());
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::free(bool fail_on_used_buffers)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);

    bool used_buffers_exist = !m_used_buffers.empty();
    if (used_buffers_exist)
    {
        if (fail_on_used_buffers)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "{}: There are still {} used buffers in the bucket, {} are free", m_name,
                                  m_used_buffers.size(), m_available_buffers.size());
        }
        else
        {
            LOGGER__MODULE__TRACE(
                MODULE_NAME, "{}: There are still {} used buffers in the bucket, {} are free. Moving to available.",
                m_name, m_used_buffers.size(), m_available_buffers.size());
        }

        for (intptr_t buffer_ptr : m_used_buffers)
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "{}: Freeing bucket: buffer {} still used", m_name, (void *)buffer_ptr);
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
            LOGGER__MODULE__ERROR(MODULE_NAME, "{}: Failed to release buffer. status code {}", m_name, result);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        m_available_buffers.pop_front();
    }

    if (fail_on_used_buffers && used_buffers_exist)
    {
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__MODULE__TRACE(MODULE_NAME,
                          "{}: After freeing bucket of size {} num of buffers {}, used buffers {} available buffers {}",
                          m_name, m_buffer_size, m_num_buffers, m_used_buffers.size(), m_available_buffers.size());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::acquire(intptr_t *buffer_ptr)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);

    if (m_available_buffers.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "{}: Buffer acquire failed - no available buffers remaining, "
                              "please validate the max buffers size you set ({})",
                              m_name, m_num_buffers);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    *buffer_ptr = m_available_buffers.front();
    m_available_buffers.pop_front();
    m_used_buffers.insert(*buffer_ptr);

    HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER(m_used_buffers.size(), m_counter_track, MEDIA_LIBRARY_CATEGORY);
    LOGGER__MODULE__TRACE(MODULE_NAME, "{}: After acquiring buffer {}, available_buffers={} used_buffers={}", m_name,
                          *buffer_ptr, m_available_buffers.size(), m_used_buffers.size());

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

    HAILO_MEDIA_LIBRARY_TRACE_CUSTOM_COUNTER(m_used_buffers.size(), m_counter_track, MEDIA_LIBRARY_CATEGORY);
    LOGGER__MODULE__TRACE(
        MODULE_NAME, "{}: After release buffer {}, total_buffers={}  available_buffers={} used_buffers={}, removed={}",
        m_name, buffer_ptr, m_num_buffers, m_available_buffers.size(), m_used_buffers.size(), num_erased);

    if (m_used_buffers.empty())
    {
        m_bucket_cv.notify_all();
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return HailoBucket::wait_for_used_buffers(const std::chrono::milliseconds &timeout_ms)
{
    std::unique_lock<std::mutex> lock(*m_bucket_mutex);
    if (m_used_buffers.empty())
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: Waiting for bucket of size {} num of buffers {}", m_name, m_buffer_size,
                          m_num_buffers);

    if (!m_bucket_cv.wait_for(lock, timeout_ms, [this]() { return m_used_buffers.empty(); }))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "{}: Timeout waiting for used buffers to be released", m_name);
        return MEDIA_LIBRARY_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

// -- Async allocation infrastructure (file-local) --

struct AsyncAllocRequest
{
    uint32_t chunk_index;    ///< Chunk number (0 = first async chunk, i.e. overall chunk 1)
    uint32_t request_number; ///< Order in which the pool called init()
    HailoBucketPtr bucket;
    size_t count;                 ///< Number of buffers to allocate in this chunk
    std::atomic<size_t> *pending; ///< Non-owning pointer to pool's pending counter
    std::atomic<bool> *failed;    ///< Non-owning pointer to pool's failure flag
    std::condition_variable *cv;  ///< Non-owning pointer to pool's CV
    std::mutex *cv_mutex;         ///< Non-owning pointer to pool's CV mutex

    // Priority: lower chunk_index first (so all pools get their first async chunk before
    // any pool gets its second), then by request_number (init() call order) to break ties.
    bool operator>(const AsyncAllocRequest &other) const
    {
        if (chunk_index != other.chunk_index)
            return chunk_index > other.chunk_index;
        return request_number > other.request_number;
    }
};

struct AsyncWorkerState
{
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::priority_queue<AsyncAllocRequest, std::vector<AsyncAllocRequest>, std::greater<AsyncAllocRequest>> queue;
    std::thread worker_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<uint32_t> request_counter{0};

    ~AsyncWorkerState()
    {
        if (running.load())
        {
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                stop_requested.store(true);
            }
            queue_cv.notify_all();
            if (worker_thread.joinable())
                worker_thread.join();
        }
    }
};

static AsyncWorkerState &get_async_state()
{
    static AsyncWorkerState state;
    return state;
}

void MediaLibraryBufferPool::async_worker_loop()
{
    auto &state = get_async_state();
    while (true)
    {
        AsyncAllocRequest request;
        {
            std::unique_lock<std::mutex> lock(state.queue_mutex);
            state.queue_cv.wait(lock, [&state] { return !state.queue.empty() || state.stop_requested.load(); });
            if (state.stop_requested.load() && state.queue.empty())
                break;
            request = state.queue.top();
            state.queue.pop();
        }

        LOGGER__MODULE__DEBUG(MODULE_NAME,
                              "Async worker: allocating chunk_index={} request_number={} count={} for bucket",
                              request.chunk_index, request.request_number, request.count);

        media_library_return ret = request.bucket->allocate_chunk(request.count);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            request.failed->store(true);
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "Async chunk allocation failed for bucket (chunk_index={} request_number={})",
                                  request.chunk_index, request.request_number);
        }
        else
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Async worker: completed chunk_index={} request_number={} count={}",
                                  request.chunk_index, request.request_number, request.count);
        }

        // Decrement pending counter and notify after every chunk so waiters can proceed
        // as soon as buffers become available (not only when all chunks are done)
        size_t remaining = --(*request.pending);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Async worker: remaining async items for this pool: {}", remaining);
        {
            std::lock_guard<std::mutex> cv_lock(*request.cv_mutex);
            request.cv->notify_all();
        }
        if (remaining == 0)
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "Async worker: all async chunks completed for request_number={}",
                                 request.request_number);
        }
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Async buffer pool worker thread exiting");
}

void MediaLibraryBufferPool::ensure_worker_started()
{
    auto &state = get_async_state();
    std::lock_guard<std::mutex> lock(state.queue_mutex);
    if (!state.running.load())
    {
        state.running.store(true);
        state.worker_thread = std::thread(async_worker_loop);
        LOGGER__MODULE__INFO(MODULE_NAME, "Async buffer pool worker thread started");
    }
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

    LOGGER__MODULE__INFO(MODULE_NAME, "Creating buffer pool with name {}", m_name);

    m_buffer_pool_mutex = std::make_shared<std::mutex>();

    auto bucket_sizes = compute_bucket_sizes(bytes_per_line, height, format);
    static const std::vector<std::string> nv12_suffixes = {"_y", "_uv"};
    for (size_t i = 0; i < bucket_sizes.size(); i++)
    {
        std::string suffix = (format == HAILO_FORMAT_NV12) ? nv12_suffixes[i] : "";
        m_buckets.emplace_back(
            std::make_shared<HailoBucket>(bucket_sizes[i], max_buffers, memory_type, m_name + suffix));
    }
}

MediaLibraryBufferPool::MediaLibraryBufferPool(uint width, uint height, HailoFormat format, size_t max_buffers,
                                               HailoMemoryType memory_type, std::string owner_name)
    : MediaLibraryBufferPool(width, height, format, max_buffers, memory_type, width, owner_name)
{
}

MediaLibraryBufferPool::~MediaLibraryBufferPool()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Destroying buffer pool with name {}", m_name);
    // Each acquired buffer holds a shared_ptr to this pool, so the destructor should only run after all buffers have
    // been released.
    for (const auto &bucket : m_buckets)
    {
        auto used_count = bucket->used_buffers_count();
        if (used_count > 0)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME,
                                  "{}: {} buffers still marked as used at destruction time - "
                                  "this indicates a buffer lifecycle bug",
                                  m_name, used_count);
        }
    }
    free();
}

void MediaLibraryBufferPool::set_on_release_callback(std::function<void(void *)> callback)
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
    m_on_release_callback = std::move(callback);
}

media_library_return MediaLibraryBufferPool::wait_for_used_buffers(const std::chrono::milliseconds &timeout_ms)
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
    for (uint8_t i = 0; i < m_buckets.size(); i++)
    {
        if (m_buckets[i]->wait_for_used_buffers(timeout_ms) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "{}: Timeout waiting for bucket {} to release buffers", m_name, i);
            return MEDIA_LIBRARY_ERROR;
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::free(bool fail_on_used_buffers)
{
    // Block until all async allocations complete — the worker thread holds
    // bucket shared_ptrs, so freeing buckets while it's running is unsafe.
    // Use infinite wait: we cannot safely proceed while pending > 0.
    {
        std::unique_lock<std::mutex> lock(m_async_cv_mutex);
        m_async_cv.wait(lock, [this]() { return m_async_pending.load() == 0; });
    }

    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
    LOGGER__MODULE__INFO(MODULE_NAME, "{}: Starting free operation, DMA free memory: {} MB", m_name,
                         DmaMemoryAllocator::get_instance().get_free_memory_mb());

    for (uint8_t i = 0; i < m_buckets.size(); i++)
    {
        HailoBucketPtr &bucket = m_buckets[i];
        LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: Freeing bucket {} of size {} num of buffers {}", m_name, i,
                              bucket->m_buffer_size, bucket->m_num_buffers);
        if (bucket->free(fail_on_used_buffers) != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "{}: failed to free bucket {}", m_name, i);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: Finished free operation, DMA free memory: {} MB", m_name,
                          DmaMemoryAllocator::get_instance().get_free_memory_mb());

    return MEDIA_LIBRARY_SUCCESS;
}

size_t MediaLibraryBufferPool::prealloc_cache_total_buffers()
{
    std::lock_guard<std::mutex> lock(s_prealloc_mutex);
    size_t total = 0;
    for (const auto &[sz, deq] : s_prealloc_buffers)
        total += deq.size();
    return total;
}

void MediaLibraryBufferPool::clear_prealloc_cache()
{
    // Stop any in-flight bg prealloc thread before freeing buffers, otherwise
    // it would race in fresh allocations after we cleared the map.
    // Joining must happen without s_prealloc_mutex held — the bg thread takes
    // that mutex per-buffer to publish its allocation.
    s_prealloc_cancel.store(true);
    if (s_prealloc_thread.joinable())
    {
        s_prealloc_thread.join();
    }
    s_prealloc_cancel.store(false);

    std::lock_guard<std::mutex> lock(s_prealloc_mutex);
    for (auto &[sz, deq] : s_prealloc_buffers)
    {
        for (intptr_t buf : deq)
            DmaMemoryAllocator::get_instance().free_dma_buffer(reinterpret_cast<void *>(buf));
    }
    s_prealloc_buffers.clear();
}

media_library_return MediaLibraryBufferPool::preallocate_buffers(uint width, uint height, HailoFormat format,
                                                                 size_t count, uint bytes_per_line)
{
    if (bytes_per_line == 0)
        bytes_per_line = width;

    auto bucket_sizes = compute_bucket_sizes(bytes_per_line, height, format);
    if (bucket_sizes.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "preallocate_buffers: unsupported format {}", static_cast<int>(format));
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME,
                         "Pre-allocating {} buffers for {}x{} format={} bytes_per_line={} ({} bucket sizes)", count,
                         width, height, static_cast<int>(format), bytes_per_line, bucket_sizes.size());

    for (size_t buffer_size : bucket_sizes)
    {
        for (size_t i = 0; i < count; i++)
        {
            void *buffer = nullptr;
            media_library_return result = DmaMemoryAllocator::get_instance().allocate_dma_buffer(buffer_size, &buffer);
            if (result != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME,
                                      "Pre-allocation failed for buffer size {} at index {}, skipping remaining",
                                      buffer_size, i);
                break;
            }
            std::lock_guard<std::mutex> lock(s_prealloc_mutex);
            s_prealloc_buffers[buffer_size].push_back(reinterpret_cast<intptr_t>(buffer));
            LOGGER__MODULE__INFO(MODULE_NAME, "Pre-alloc: allocated buffer {}/{} of size {} for {}x{} format={}", i + 1,
                                 count, buffer_size, width, height, static_cast<int>(format));
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "Pre-allocated {} buffers of size {} bytes", count, buffer_size);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

// -- Prealloc item helpers (used by preallocate_from_config) --

struct PreallocItem
{
    uint width;
    uint height;
    HailoFormat format;
    size_t count;
    uint bytes_per_line;
};

static std::vector<PreallocItem> collect_freeze_denoise_dewarp_items(const frontend_config_t &config)
{
    std::vector<PreallocItem> items;
    auto input_width = static_cast<uint>(config.input_config.resolution.dimensions.destination_width);
    auto input_height = static_cast<uint>(config.input_config.resolution.dimensions.destination_height);

    // Image freeze output pool
    items.push_back({input_width, input_height, HAILO_FORMAT_NV12, 1, 0});

    // Post-ISP denoise output pool (rounded up to alignment boundary)
    if (config.denoise_config.enabled && !config.denoise_config.bayer)
    {
        static constexpr uint DENOISE_ALIGNMENT = 16;
        uint dw = ((input_width + DENOISE_ALIGNMENT - 1) / DENOISE_ALIGNMENT) * DENOISE_ALIGNMENT;
        uint dh = ((input_height + DENOISE_ALIGNMENT - 1) / DENOISE_ALIGNMENT) * DENOISE_ALIGNMENT;
        items.push_back({dw, dh, HAILO_FORMAT_NV12, 1, 0});
    }

    // Dewarp output pool (DSP stride)
    if (config.ldc_config.dewarp_config.enabled)
    {
        auto bpl = static_cast<uint>(dsp_utils::get_dsp_desired_stride_from_width(input_width));
        items.push_back({input_width, input_height, HAILO_FORMAT_NV12, 1, bpl});
    }
    return items;
}

static std::vector<PreallocItem> collect_multi_resize_and_motion_detection_items(const multi_resize_config_t &mr_config)
{
    std::vector<PreallocItem> items;
    static constexpr size_t INITIAL_CHUNK_PREALLOC_COUNT = 3;

    auto &outputs = mr_config.application_input_streams_config.resolutions;
    HailoFormat format = mr_config.application_input_streams_config.format;
    bool rotated = mr_config.rotation_config.effective_value() == ROTATION_ANGLE_90 ||
                   mr_config.rotation_config.effective_value() == ROTATION_ANGLE_270;

    for (const auto &res : outputs)
    {
        uint w = res.dimensions.destination_width;
        uint h = res.dimensions.destination_height;
        if (rotated)
            std::swap(w, h);
        auto bpl = static_cast<uint>(dsp_utils::get_dsp_desired_stride_from_width(w));
        items.push_back({w, h, format, INITIAL_CHUNK_PREALLOC_COUNT, bpl});
    }

    // Motion detection output pool
    if (mr_config.motion_detection_config.enabled)
    {
        auto &md_res = mr_config.motion_detection_config.resolution;
        uint w = md_res.dimensions.destination_width;
        uint h = md_res.dimensions.destination_height;
        if (rotated)
            std::swap(w, h);
        auto bpl = static_cast<uint>(dsp_utils::get_dsp_desired_stride_from_width(w));
        items.push_back({w, h, HAILO_FORMAT_GRAY8, 1, bpl});
    }
    return items;
}

static std::vector<PreallocItem> collect_encoder_and_privacy_mask_items(
    const std::shared_ptr<config_profile_t> &profile)
{
    std::vector<PreallocItem> items;
    if (!profile)
        return items;

    static constexpr uint PRIVACY_MASK_LINE_DIVISION = 32;
    static constexpr uint PRIVACY_MASK_HEIGHT_DIVISOR = 4;
    static constexpr uint PRIVACY_MASK_BPL_ALIGNMENT = 8;

    for (const auto &[stream_id, stream] : profile->encoded_output_streams)
    {
        if (!std::holds_alternative<hailo_encoder_config_t>(stream.encoding))
            continue;

        const auto &enc = std::get<hailo_encoder_config_t>(stream.encoding);
        uint w = enc.input_stream.width;
        uint h = enc.input_stream.height;
        size_t gop_size = enc.gop.gop_size;
        LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: encoder stream '{}' {}x{} gop_size={}", stream_id,
                             w, h, gop_size);
        items.push_back({w, h, HAILO_FORMAT_GRAY8, gop_size, 0});

        // Privacy mask output pool
        uint pm_width =
            ((w + (PRIVACY_MASK_LINE_DIVISION - 1)) & ~(PRIVACY_MASK_LINE_DIVISION - 1)) / PRIVACY_MASK_LINE_DIVISION;
        uint pm_height = h / PRIVACY_MASK_HEIGHT_DIVISOR;
        uint pm_bpl = (pm_width + (PRIVACY_MASK_BPL_ALIGNMENT - 1)) & ~(PRIVACY_MASK_BPL_ALIGNMENT - 1);
        items.push_back({pm_width, pm_height, HAILO_FORMAT_GRAY8, 1, pm_bpl});
    }
    return items;
}

void MediaLibraryBufferPool::preallocate_from_config(const ConfigManagerInteractor &interactor)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: ENTERED");

    // Reap any prior bg thread before launching a new one. Under normal
    // start/stop sequencing clear_prealloc_cache() will already have joined
    // it, but we cannot move-assign over a joinable std::thread.
    if (s_prealloc_thread.joinable())
    {
        s_prealloc_cancel.store(true);
        s_prealloc_thread.join();
    }
    s_prealloc_cancel.store(false);

    // Guard against double invocation (can be called from both property setter and state change).
    // Uses the prealloc cache itself as the guard — if already populated, skip.
    // This naturally resets after pipeline teardown when pools consume the cache.
    {
        std::lock_guard<std::mutex> lock(s_prealloc_mutex);
        if (!s_prealloc_buffers.empty())
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: cache already populated ({} sizes), skipping",
                                 s_prealloc_buffers.size());
            return;
        }
    }

    frontend_config_t config = interactor.get_frontend_config();
    auto profile = interactor.get_default_profile();

    auto items = collect_freeze_denoise_dewarp_items(config);
    auto mr_items = collect_multi_resize_and_motion_detection_items(config.multi_resize_config);
    auto enc_items = collect_encoder_and_privacy_mask_items(profile);
    items.insert(items.end(), std::make_move_iterator(mr_items.begin()), std::make_move_iterator(mr_items.end()));
    items.insert(items.end(), std::make_move_iterator(enc_items.begin()), std::make_move_iterator(enc_items.end()));

    if (items.empty())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: no items to preallocate");
        return;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: launching background thread for {} items",
                         items.size());
    for (size_t i = 0; i < items.size(); i++)
    {
        LOGGER__MODULE__INFO(MODULE_NAME,
                             "preallocate_from_config: item[{}] {}x{} format={} count={} bytes_per_line={}", i,
                             items[i].width, items[i].height, static_cast<int>(items[i].format), items[i].count,
                             items[i].bytes_per_line);
    }

    s_prealloc_thread = std::thread([items = std::move(items)]() {
        LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: background thread STARTED");
        for (const auto &item : items)
        {
            if (s_prealloc_cancel.load())
            {
                LOGGER__MODULE__INFO(MODULE_NAME, "preallocate_from_config: cancelled before completing all items");
                return;
            }
            preallocate_buffers(item.width, item.height, item.format, item.count, item.bytes_per_line);
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "Background preallocation completed ({} items)", items.size());
    });
}

media_library_return MediaLibraryBufferPool::init(size_t initial_chunk_size)
{
    {
        std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
        LOGGER__MODULE__INFO(MODULE_NAME, "{}: Starting init operation, DMA free memory: {} MB", m_name,
                             DmaMemoryAllocator::get_instance().get_free_memory_mb());

        // Allocate first chunk synchronously (just enough to serve immediate acquire requests)
        for (HailoBucketPtr &bucket : m_buckets)
        {
            size_t first_chunk = std::min(initial_chunk_size, m_max_buffers);
            if (bucket->allocate_chunk(first_chunk, /*use_prealloc=*/true) != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "{}: failed to allocate first chunk", m_name);
                return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
            }
        }

        LOGGER__MODULE__INFO(MODULE_NAME, "{}: First chunk ({}) allocated synchronously, DMA free memory: {} MB",
                             m_name, std::min(initial_chunk_size, m_max_buffers),
                             DmaMemoryAllocator::get_instance().get_free_memory_mb());

        // If all buffers fit in the first chunk, no async work needed
        if (m_max_buffers <= initial_chunk_size)
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "{}: All {} buffers fit in first chunk, no async allocation needed",
                                 m_name, m_max_buffers);
            return MEDIA_LIBRARY_SUCCESS;
        }
    }
    // m_buffer_pool_mutex released — queue async work without holding the pool lock

    auto &state = get_async_state();
    uint32_t request_number = state.request_counter.fetch_add(1);
    size_t remaining = m_max_buffers - initial_chunk_size;

    // Count total async chunks (across all buckets)
    size_t num_chunks = (remaining + CHUNK_SIZE - 1) / CHUNK_SIZE;
    size_t total_async_items = num_chunks * m_buckets.size();

    m_async_pending.store(total_async_items);
    m_async_failed.store(false);

    ensure_worker_started();

    {
        std::lock_guard<std::mutex> qlock(state.queue_mutex);
        size_t offset = initial_chunk_size;
        uint32_t chunk_index = 0;
        while (offset < m_max_buffers)
        {
            size_t chunk_count = std::min(CHUNK_SIZE, m_max_buffers - offset);
            for (const HailoBucketPtr &bucket : m_buckets)
            {
                AsyncAllocRequest req;
                req.chunk_index = chunk_index;
                req.request_number = request_number;
                req.bucket = bucket;
                req.count = chunk_count;
                req.pending = &m_async_pending;
                req.failed = &m_async_failed;
                req.cv = &m_async_cv;
                req.cv_mutex = &m_async_cv_mutex;
                state.queue.push(std::move(req));
            }
            offset += chunk_count;
            chunk_index++;
        }
    }
    state.queue_cv.notify_one();

    LOGGER__MODULE__INFO(
        MODULE_NAME,
        "{}: Queued {} async allocation items (request_number={}, remaining_buffers={}, num_chunks={}, buckets={})",
        m_name, total_async_items, request_number, remaining, num_chunks, m_buckets.size());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::wait_for_all_buffers_allocated(const std::chrono::milliseconds &timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_async_cv_mutex);
    if (m_async_pending.load() == 0)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "{}: wait_for_all_buffers_allocated - no pending async work", m_name);
        return m_async_failed.load() ? MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR : MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "{}: Waiting for {} async allocation items to complete (timeout={}ms)", m_name,
                         m_async_pending.load(), timeout_ms.count());

    if (!m_async_cv.wait_for(lock, timeout_ms, [this]() { return m_async_pending.load() == 0; }))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "{}: Timeout waiting for async buffer allocation", m_name);
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_async_failed.load())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "{}: One or more async buffer allocations failed", m_name);
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "{}: All async buffers allocated successfully", m_name);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryBufferPool::for_each_buffer(std::function<bool(int, size_t)> func)
{
    // Wait for all async chunks to be allocated before iterating buffers
    wait_for_all_buffers_allocated();

    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);

    for (HailoBucketPtr &bucket : m_buckets)
    {
        std::unique_lock<std::mutex> bucket_lock(*bucket->m_bucket_mutex);
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
    // If no buffers are available but async allocation is in progress,
    // wait for the next chunk to complete (not all chunks) so we can proceed ASAP
    if (m_async_pending.load() > 0 && get_available_buffers_count() == 0)
    {
        std::unique_lock<std::mutex> lock(m_async_cv_mutex);
        m_async_cv.wait(lock, [this]() { return get_available_buffers_count() > 0 || m_async_pending.load() == 0; });
    }

    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);

    m_buffer_index++;
    if (m_buffer_index > m_max_buffers)
        m_buffer_index = 1;
    LOGGER__MODULE__TRACE(MODULE_NAME, "{}: Acquiring buffer number {}", m_name, m_buffer_index);
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
            LOGGER__MODULE__ERROR(MODULE_NAME, "CMA memory not supported");
            m_buckets[0]->release(y_channel_ptr);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        // Gather uv channel info
        intptr_t uv_channel_ptr;
        ret = m_buckets[1]->acquire(&uv_channel_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            m_buckets[0]->release(y_channel_ptr);
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
            LOGGER__MODULE__ERROR(MODULE_NAME, "CMA memory not supported");
            m_buckets[0]->release(y_channel_ptr);
            m_buckets[1]->release(uv_channel_ptr);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        LOGGER__MODULE__TRACE(MODULE_NAME,
                              "{}: Buffers acquired: buffer for y_channel (size = {}), and "
                              "uv_channel (size = {})",
                              m_name, y_channel_size, uv_channel_size);

        // Fill in buffer_data values
        HailoBufferDataPtr buffer_data = std::make_shared<hailo_buffer_data_t>(
            (size_t)m_width, (size_t)m_height, (size_t)2, HAILO_FORMAT_NV12, memory_type,
            std::vector<hailo_data_plane_t>{y_plane_data, uv_plane_data});

        ret = buffer->create(shared_from_this(), buffer_data, m_on_release_callback, nullptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            m_buckets[0]->release(y_channel_ptr);
            m_buckets[1]->release(uv_channel_ptr);
            return ret;
        }
        buffer->set_buffer_index(m_buffer_index);
        LOGGER__MODULE__TRACE(MODULE_NAME, "{}: NV12 Buffer width {} height {} acquired", m_name,
                              buffer->buffer_data->width, buffer->buffer_data->height);
        break;
    }
    case HAILO_FORMAT_RGB: {
        size_t rgb_stride = m_bytes_per_line * 3;
        size_t rgb_size = rgb_stride * m_height;
        intptr_t rgb_ptr;

        ret = m_buckets[0]->acquire(&rgb_ptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        hailo_data_plane_t plane_data;
        plane_data.bytesperline = rgb_stride;
        plane_data.bytesused = rgb_size;

        int rgb_fd;
        ret = DmaMemoryAllocator::get_instance().get_fd((void *)rgb_ptr, rgb_fd);

        HailoMemoryType memory_type;
        plane_data.userptr = (void *)rgb_ptr;
        if (ret == MEDIA_LIBRARY_SUCCESS)
        {
            plane_data.fd = rgb_fd;
            memory_type = HAILO_MEMORY_TYPE_DMABUF;
        }
        else
        {
            memory_type = HAILO_MEMORY_TYPE_CMA;
            LOGGER__MODULE__ERROR(MODULE_NAME, "CMA memory not supported");
            m_buckets[0]->release(rgb_ptr);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        // Fill in buffer_data values
        HailoBufferDataPtr buffer_data =
            std::make_shared<hailo_buffer_data_t>((size_t)m_width, (size_t)m_height, (size_t)1, HAILO_FORMAT_RGB,
                                                  memory_type, std::vector<hailo_data_plane_t>{plane_data});

        ret = buffer->create(shared_from_this(), buffer_data, m_on_release_callback, nullptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            m_buckets[0]->release(rgb_ptr);
            return ret;
        }
        buffer->set_buffer_index(m_buffer_index);

        LOGGER__MODULE__TRACE(MODULE_NAME, "{}: RGB Buffer width {} height {} acquired", m_name,
                              buffer->buffer_data->width, buffer->buffer_data->height);
        break;
    }
    case HAILO_FORMAT_GRAY8:
    case HAILO_FORMAT_GRAY12:
    case HAILO_FORMAT_GRAY16: {
        size_t image_stride;
        if (m_format == HAILO_FORMAT_GRAY8)
        {
            image_stride = m_bytes_per_line;
        }
        else if (m_format == HAILO_FORMAT_GRAY12)
        {
            image_stride = m_bytes_per_line * 1.5;
        }
        else
        {
            image_stride = m_bytes_per_line * 2;
        }
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
            LOGGER__MODULE__ERROR(MODULE_NAME, "CMA memory not supported");
            m_buckets[0]->release(data_ptr);
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }

        // Fill in buffer_data values
        HailoBufferDataPtr buffer_data;
        if (m_format == HAILO_FORMAT_GRAY8)
        {
            buffer_data =
                std::make_shared<hailo_buffer_data_t>((size_t)m_width, (size_t)m_height, (size_t)1, HAILO_FORMAT_GRAY8,
                                                      memory_type, std::vector<hailo_data_plane_t>{plane_data});
        }
        else if (m_format == HAILO_FORMAT_GRAY12)
        {
            buffer_data =
                std::make_shared<hailo_buffer_data_t>((size_t)m_width, (size_t)m_height, (size_t)1, HAILO_FORMAT_GRAY12,
                                                      memory_type, std::vector<hailo_data_plane_t>{plane_data});
        }
        else
        {
            buffer_data =
                std::make_shared<hailo_buffer_data_t>((size_t)m_width, (size_t)m_height, (size_t)1, HAILO_FORMAT_GRAY16,
                                                      memory_type, std::vector<hailo_data_plane_t>{plane_data});
        }

        ret = buffer->create(shared_from_this(), buffer_data, m_on_release_callback, nullptr);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            m_buckets[0]->release(data_ptr);
            return ret;
        }

        if (m_format == HAILO_FORMAT_GRAY8)
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "{}: GRAY8 Buffer width {} height {} acquired", m_name,
                                  buffer->buffer_data->width, buffer->buffer_data->height);
        }
        else if (m_format == HAILO_FORMAT_GRAY12)
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "{}: GRAY12 Buffer width {} height {} acquired", m_name,
                                  buffer->buffer_data->width, buffer->buffer_data->height);
        }
        else
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "{}: GRAY16 Buffer width {} height {} acquired", m_name,
                                  buffer->buffer_data->width, buffer->buffer_data->height);
        }
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
    // Return the minimum across all buckets — for NV12 with Y+UV buckets,
    // we need both to have available buffers before an acquire can succeed.
    int min_available = std::numeric_limits<int>::max();
    for (const auto &bucket : m_buckets)
    {
        min_available = std::min(min_available, bucket->available_buffers_count());
    }
    return min_available;
}

media_library_return MediaLibraryBufferPool::release_plane(hailo_media_library_buffer *buffer, uint32_t plane_index)
{
    auto bucket = m_buckets[plane_index];
    LOGGER__MODULE__TRACE(
        MODULE_NAME,
        "{}: Releasing plane {} of buffer with index {} of bucket of size {} num buffers {} used buffers {}", m_name,
        plane_index, buffer->buffer_index, bucket->m_buffer_size, bucket->m_num_buffers,
        bucket->m_used_buffers.size() - 1);

    if (buffer->is_dmabuf())
    {
        return bucket->release((intptr_t)buffer->get_plane_ptr(plane_index));
    }

    return bucket->release((intptr_t)buffer->buffer_data->planes[plane_index].userptr);
}

media_library_return MediaLibraryBufferPool::release_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*m_buffer_pool_mutex);
    for (uint32_t i = 0; i < m_buckets.size(); i++)
    {
        if (m_buckets[i]->m_used_buffers.size() > 0)
        {
            media_library_return ret = release_plane(buffer.get(), i);
            if (ret != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "{}: failed to release plane number {}", m_name, i);
                return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
            }
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}
