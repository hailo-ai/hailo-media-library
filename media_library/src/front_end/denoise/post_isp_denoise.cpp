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

#include "post_isp_denoise.hpp"
#include "hailort_denoise.hpp"
#include "denoise_common.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"

#include <iostream>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <string>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <ctime>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

#define MODULE_NAME LoggerType::Denoise

class MediaLibraryPostIspDenoise::Impl final
{
  public:
    Impl();
    ~Impl();
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

    // Configure the denoise module with new json string
    media_library_return configure(const std::string &config_string);

    // Configure the denoise module with denoise_config_t and hailort_t object
    media_library_return configure(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs);

    // Perform pre-processing on the input frame and return the output frames
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame);

    // get the denoise configurations object
    denoise_config_t get_denoise_configs();

    // get the hailort configurations object
    hailort_t get_hailort_configs();

    // get the enabled config status
    bool is_enabled();

    // set input frame dimensions for dynamic buffer pool creation
    media_library_return set_input_dimensions(uint32_t width, uint32_t height);

    // set the callbacks object
    media_library_return observe(const MediaLibraryPostIspDenoise::callbacks_t &callbacks);

  private:
    static constexpr size_t QUEUE_DEFAULT_SIZE = 4;

    static constexpr int HAILORT_SCHEDULER_THRESHOLD = 1;
    static constexpr std::chrono::milliseconds HAILORT_SCHEDULER_TIMEOUT{1000};
    static constexpr int HAILORT_SCHEDULER_BATCH_SIZE = 2;

    static constexpr size_t BUFFER_POOL_MAX_BUFFERS = 10;
    // Default buffer pool dimensions (used as fallback)
    static constexpr size_t DEFAULT_BUFFER_POOL_BUFFER_WIDTH = 3840;
    static constexpr size_t DEFAULT_BUFFER_POOL_BUFFER_HEIGHT = 2160;
    static constexpr const char *BUFFER_POOL_NAME = "post_isp_denoise_output";

    // configured flag - to determine if first configuration was done
    // configuration manager
    ConfigManager m_denoise_config_manager;
    ConfigManager m_hailort_config_manager;
    std::vector<MediaLibraryPostIspDenoise::callbacks_t> m_callbacks;
    // output buffer pool
    std::shared_ptr<MediaLibraryBufferPool> m_output_buffer_pool;
    // operation configurations
    denoise_config_t m_denoise_configs;
    hailort_t m_hailort_configs;
    // configuration mutex
    std::shared_mutex rw_lock;
    // HRT module
    HailortAsyncDenoise m_hailort_denoise;
    // loopback controls
    uint8_t m_queue_size = QUEUE_DEFAULT_SIZE;
    uint8_t m_loop_counter = 0;
    uint8_t m_loopback_batch_counter = 0;
    uint8_t m_loopback_limit = 1;
    bool m_configured = false;
    std::atomic<bool> m_flushing = false;

    std::condition_variable m_loopback_condvar;
    std::mutex m_loopback_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_loopback_queue;
    // callback controls
    std::condition_variable m_inference_callback_condvar;
    std::mutex m_inference_callback_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_inference_callback_queue;

    media_library_return create_and_initialize_buffer_pools(uint32_t width = DEFAULT_BUFFER_POOL_BUFFER_WIDTH,
                                                            uint32_t height = DEFAULT_BUFFER_POOL_BUFFER_HEIGHT);
    media_library_return decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs,
                                                   std::string config_string);
    media_library_return perform_denoise(HailoMediaLibraryBufferPtr input_buffer,
                                         HailoMediaLibraryBufferPtr output_buffer);
    media_library_return perform_initial_batch(HailoMediaLibraryBufferPtr input_buffer,
                                               HailoMediaLibraryBufferPtr output_buffer);
    media_library_return perform_subsequent_batches(HailoMediaLibraryBufferPtr input_buffer,
                                                    HailoMediaLibraryBufferPtr output_buffer);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void inference_callback(HailoMediaLibraryBufferPtr output_buffer);
    void queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_loopback_buffer();
    void clear_loopback_queue();
    void queue_buffer(HailoMediaLibraryBufferPtr buffer, std::queue<HailoMediaLibraryBufferPtr> &queue,
                      std::shared_ptr<std::mutex> mutex, std::shared_ptr<std::condition_variable> condvar,
                      uint8_t queue_size);
    HailoMediaLibraryBufferPtr dequeue_buffer(std::queue<HailoMediaLibraryBufferPtr> &queue,
                                              std::shared_ptr<std::mutex> mutex,
                                              std::shared_ptr<std::condition_variable> condvar);
    void clear_queue(std::queue<HailoMediaLibraryBufferPtr> &queue, std::shared_ptr<std::mutex> mutex,
                     std::shared_ptr<std::condition_variable> condvar);
    void inference_callback_thread();
    std::thread m_inference_callback_thread;
    void queue_inference_callback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_inference_callback_buffer();
};

//------------------------ MediaLibraryPostIspDenoise ------------------------

MediaLibraryPostIspDenoise::MediaLibraryPostIspDenoise()
{
    m_impl = std::make_unique<MediaLibraryPostIspDenoise::Impl>();
}

MediaLibraryPostIspDenoise::~MediaLibraryPostIspDenoise() = default;

media_library_return MediaLibraryPostIspDenoise::configure(const std::string &config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryPostIspDenoise::configure(const denoise_config_t &denoise_configs,
                                                           const hailort_t &hailort_configs)
{
    return m_impl->configure(denoise_configs, hailort_configs);
}

media_library_return MediaLibraryPostIspDenoise::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                              HailoMediaLibraryBufferPtr output_frame)
{
    return m_impl->handle_frame(input_frame, output_frame);
}

denoise_config_t MediaLibraryPostIspDenoise::get_denoise_configs()
{
    return m_impl->get_denoise_configs();
}

hailort_t MediaLibraryPostIspDenoise::get_hailort_configs()
{
    return m_impl->get_hailort_configs();
}

bool MediaLibraryPostIspDenoise::is_enabled()
{
    return m_impl->is_enabled();
}

media_library_return MediaLibraryPostIspDenoise::set_input_dimensions(uint32_t width, uint32_t height)
{
    return m_impl->set_input_dimensions(width, height);
}

media_library_return MediaLibraryPostIspDenoise::observe(const MediaLibraryPostIspDenoise::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

//------------------------ MediaLibraryPostIspDenoise::Impl ------------------------

MediaLibraryPostIspDenoise::Impl::Impl()
    : m_denoise_config_manager(ConfigSchema::CONFIG_SCHEMA_DENOISE),
      m_hailort_config_manager(ConfigSchema::CONFIG_SCHEMA_HAILORT),
      m_hailort_denoise([this](HailoMediaLibraryBufferPtr output_buffer) { inference_callback(output_buffer); })
{
    // Buffer pool will be created dynamically based on input frame dimensions
}

MediaLibraryPostIspDenoise::Impl::~Impl()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Denoise - destructor");
    m_flushing = true;
    m_inference_callback_condvar.notify_one();
    m_loopback_condvar.notify_one();
    if (m_inference_callback_thread.joinable())
        m_inference_callback_thread.join();
    clear_loopback_queue();
}

media_library_return MediaLibraryPostIspDenoise::Impl::decode_config_json_string(denoise_config_t &denoise_configs,
                                                                                 hailort_t &hailort_configs,
                                                                                 std::string config_string)
{
    media_library_return hailort_status =
        m_hailort_config_manager.config_string_to_struct<hailort_t>(config_string, hailort_configs);
    if (hailort_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode Hailort config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    m_hailort_configs = hailort_configs;
    media_library_return denoise_status =
        m_denoise_config_manager.config_string_to_struct<denoise_config_t>(config_string, denoise_configs);
    if (denoise_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode denoise config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPostIspDenoise::Impl::configure(const std::string &config_string)
{
    denoise_config_t denoise_configs;
    hailort_t hailort_configs;
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring denoise Decoding json string");
    if (decode_config_json_string(denoise_configs, hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(denoise_configs, hailort_configs);
}

media_library_return MediaLibraryPostIspDenoise::Impl::configure(const denoise_config_t &denoise_configs,
                                                                 const hailort_t &hailort_configs)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring post ISP denoise");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    bool enabled_changed = denoise_common::post_isp_enable_changed(m_denoise_configs, denoise_configs);
    LOGGER__MODULE__INFO(MODULE_NAME, "NOTE: Loopback limit configurations are only applied when denoise is enabled.");

    if (!enabled_changed && !denoise_configs.enabled)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Post ISP Denoise Remains disabled, skipping configuration");
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (!denoise_configs.bayer && ((denoise_configs.network_config != m_denoise_configs.network_config) ||
                                   (hailort_configs.device_id != m_hailort_configs.device_id)))
    {
        if (!m_hailort_denoise.set_config(denoise_configs, hailort_configs.device_id, HAILORT_SCHEDULER_THRESHOLD,
                                          HAILORT_SCHEDULER_TIMEOUT, HAILORT_SCHEDULER_BATCH_SIZE))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init hailort");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // check if enabling
    if (denoise_common::post_isp_enabled(m_denoise_configs, denoise_configs))
    {
        // Buffer pool will be created dynamically when set_input_dimensions is called
        // with actual frame dimensions from GStreamer caps
        LOGGER__MODULE__INFO(MODULE_NAME, "Post ISP denoise enabled - buffer pool will be created dynamically");
        m_loop_counter = 0;
        m_loopback_batch_counter = 0;
        m_loopback_limit = denoise_configs.loopback_count;
        m_flushing = false;
        m_inference_callback_thread = std::thread(&MediaLibraryPostIspDenoise::Impl::inference_callback_thread, this);
    }

    // check if disabling
    if (denoise_common::post_isp_disabled(m_denoise_configs, denoise_configs))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Post ISP Denoise disable requested, starting flushing");
        m_flushing = true;

        m_inference_callback_condvar.notify_all();

        if (m_inference_callback_thread.joinable())
            m_inference_callback_thread.join();

        m_loopback_condvar.notify_all();
        clear_loopback_queue();

        if (m_output_buffer_pool->wait_for_used_buffers() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to wait for used buffers to be released");
            return MEDIA_LIBRARY_ERROR;
        }
        if (m_output_buffer_pool->free() != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_ERROR;
        }

	m_output_buffer_pool = nullptr;
    }

    // Call observing callbacks in case configuration changed
    for (auto &callbacks : m_callbacks)
    {
        // We use the denoise_common::post_isp_enabled to cover corner cases
        if (enabled_changed && callbacks.on_enable_changed)
            callbacks.on_enable_changed(denoise_common::post_isp_enabled(m_denoise_configs, denoise_configs));
        if (enabled_changed && callbacks.send_event)
        {
            callbacks.send_event(denoise_common::post_isp_enabled(m_denoise_configs, denoise_configs));
        }
    }

    m_denoise_configs = denoise_configs;
    m_hailort_configs = hailort_configs;
    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryPostIspDenoise::Impl::create_and_initialize_buffer_pools(uint32_t width,
                                                                                          uint32_t height)
{
    // Create buffer pool with dynamic dimensions
    m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
        width, height, HAILO_FORMAT_NV12, BUFFER_POOL_MAX_BUFFERS, HAILO_MEMORY_TYPE_DMABUF, BUFFER_POOL_NAME);

    // Create output buffer pool
    LOGGER__MODULE__DEBUG(
        MODULE_NAME, "Initalizing buffer pool named {} for output resolution: width {} height {} in buffers size of {}",
        BUFFER_POOL_NAME, width, height, BUFFER_POOL_MAX_BUFFERS);
    if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryPostIspDenoise::Impl::stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "denoise handle_frame took {} milliseconds ({} fps)", ms, framerate);
}

/**
 * Performs the first batch of de-noising
 *
 * Example of the loopback mechanism (loopback=3):
 * ----------------------------------
 * [Frame 0, concat with black frame]
 * [Frame 1, concat with black frame]
 * [Frame 2, concat with black frame]
 * [Frame 3, concat with Frame 2]
 * [Frame 4, concat with Frame 2]
 * [Frame 5, concat with Frame 2]
 * [Frame 6, concat with Frame 5]
 * [Frame 7, concat with Frame 5]
 * [Frame 8, concat with Frame 5]
 *
 * @param input_buffer The input buffer containing the data to be denoised.
 * @param output_buffer The output buffer to store the denoised data.
 * @return The status of the denoising operation. Returns MEDIA_LIBRARY_SUCCESS if successful, MEDIA_LIBRARY_ERROR
 * otherwise.
 */
media_library_return MediaLibraryPostIspDenoise::Impl::perform_initial_batch(HailoMediaLibraryBufferPtr input_buffer,
                                                                             HailoMediaLibraryBufferPtr output_buffer)
{
    if (!m_hailort_denoise.process(input_buffer, input_buffer, output_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to process denoise, during initial batch");
        return MEDIA_LIBRARY_ERROR;
    }

    if ((m_loop_counter + 1) == m_loopback_limit)
    {
        for (int i = 0; i < m_loopback_limit; i++)
        {
            queue_loopback_buffer(output_buffer);
        }
    }

    m_loop_counter++;
    m_loopback_batch_counter++;

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * Performs subsequent batches of de-noising
 *
 * @param input_buffer The input buffer containing the data to be denoised.
 * @param output_buffer The output buffer to store the denoised data.
 * @return The status of the denoising operation. Returns MEDIA_LIBRARY_SUCCESS if successful, MEDIA_LIBRARY_ERROR
 * otherwise.
 */
media_library_return MediaLibraryPostIspDenoise::Impl::perform_subsequent_batches(
    HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer)
{
    // Loopback in batches of m_loopback_limit
    if ((m_loopback_batch_counter + 1) % m_loopback_limit == 0)
    {
        m_loopback_batch_counter = 0;

        for (int i = 0; i < m_loopback_limit; i++)
        {
            queue_loopback_buffer(output_buffer);
        }
    }
    else
    {
        m_loopback_batch_counter++;
    }

    HailoMediaLibraryBufferPtr loopback_buffer = dequeue_loopback_buffer();

    if (loopback_buffer == nullptr)
    {
        if (m_flushing)
        {
            return MEDIA_LIBRARY_SUCCESS;
        }
        LOGGER__MODULE__ERROR(MODULE_NAME, "loopback buffer is null");
        return MEDIA_LIBRARY_ERROR;
    }

    if (!m_hailort_denoise.process(input_buffer, loopback_buffer, output_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to process denoise");
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Perform denoise
 * Acquire buffer for denoise output and perform denoise on
 * the NN core
 *
 * @param[in] input_buffer - pointer to the input frame
 * @param[out] output_buffer - dewarp output buffer
 */
media_library_return MediaLibraryPostIspDenoise::Impl::perform_denoise(HailoMediaLibraryBufferPtr input_buffer,
                                                                       HailoMediaLibraryBufferPtr output_buffer)
{
    // Acquire buffer for denoise output
    if (m_output_buffer_pool->acquire_buffer(output_buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for denoise output");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // check null for input and output buffer
    if (input_buffer == nullptr || output_buffer == nullptr)
    {
        // log: input or output buffer is null
        LOGGER__MODULE__ERROR(MODULE_NAME, "input or output buffer is null");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_loop_counter < m_loopback_limit)
    {
        return perform_initial_batch(input_buffer, output_buffer);
    }

    return perform_subsequent_batches(input_buffer, output_buffer);
}

media_library_return MediaLibraryPostIspDenoise::Impl::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                                    HailoMediaLibraryBufferPtr output_frame)
{
    if (!is_enabled())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Denoise is disabled - skipping handle_frame");
        return MEDIA_LIBRARY_UNINITIALIZED;
    }

    std::unique_lock<std::shared_mutex> lock(rw_lock);

    if (!(m_denoise_configs.enabled && !m_denoise_configs.bayer))
    {
        LOGGER__DEBUG("Post ISP Denoise is disabled, skipping denoise [handle_frame]");
        return MEDIA_LIBRARY_SUCCESS;
    }
    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    // Denoise
    output_frame->copy_metadata_from(input_frame);
    output_frame->isp_ae_fps = input_frame->isp_ae_fps;
    output_frame->isp_ae_converged = input_frame->isp_ae_converged;
    output_frame->isp_ae_average_luma = input_frame->isp_ae_average_luma;
    output_frame->isp_ae_integration_time = input_frame->isp_ae_integration_time;
    output_frame->isp_timestamp_ns = input_frame->isp_timestamp_ns;
    output_frame->pts = input_frame->pts;

    media_library_return media_lib_ret = perform_denoise(input_frame, output_frame);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    stamp_time_and_log_fps(start_handle, end_handle);

    return MEDIA_LIBRARY_SUCCESS;
}

denoise_config_t MediaLibraryPostIspDenoise::Impl::get_denoise_configs()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_denoise_configs;
}

hailort_t MediaLibraryPostIspDenoise::Impl::get_hailort_configs()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_hailort_configs;
}

bool MediaLibraryPostIspDenoise::Impl::is_enabled()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_denoise_configs.enabled && !m_denoise_configs.bayer;
}

media_library_return MediaLibraryPostIspDenoise::Impl::set_input_dimensions(uint32_t width, uint32_t height)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Setting input dimensions: {}x{}", width, height);


    std::unique_lock<std::shared_mutex> lock(rw_lock);

    if (!m_denoise_configs.enabled || m_denoise_configs.bayer)
    {
        LOGGER__MODULE__INFO(MODULE_NAME,
                              "Denoise is not enabled for post-ISP processing, skipping buffer pool creation");
        return MEDIA_LIBRARY_SUCCESS;
    }

    if (m_output_buffer_pool != nullptr)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Buffer pool already created, skipping re-creation");
        return MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Creating buffer pool with dynamic dimensions: {}x{}", width, height);
    return create_and_initialize_buffer_pools(width, height);
}

media_library_return MediaLibraryPostIspDenoise::Impl::observe(const MediaLibraryPostIspDenoise::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryPostIspDenoise::Impl::inference_callback_thread()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
        if (m_flushing && !m_hailort_denoise.has_pending_jobs() && m_inference_callback_queue.empty())
        {
            LOGGER__INFO("Inference callback thread is exiting");
            return;
        }
        lock.unlock();
        HailoMediaLibraryBufferPtr output_buffer = dequeue_inference_callback_buffer();
        if (output_buffer == nullptr)
        {
            continue;
        }

        for (auto &callbacks : m_callbacks)
        {
            if (callbacks.on_buffer_ready)
            {
                callbacks.on_buffer_ready(output_buffer);
            }
        }
    }
}

void MediaLibraryPostIspDenoise::Impl::inference_callback(HailoMediaLibraryBufferPtr output_buffer)
{
    queue_inference_callback_buffer(output_buffer);
}

// Loopback queue controls

void MediaLibraryPostIspDenoise::Impl::queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    m_loopback_condvar.wait(lock, [this] { return m_loopback_queue.size() < m_queue_size; });
    m_loopback_queue.push(buffer);
    m_loopback_condvar.notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryPostIspDenoise::Impl::dequeue_loopback_buffer()
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    m_loopback_condvar.wait(lock, [this] { return !m_loopback_queue.empty() || m_flushing; });
    if (m_loopback_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_loopback_queue.front();
    m_loopback_queue.pop();
    m_loopback_condvar.notify_one();
    return buffer;
}

void MediaLibraryPostIspDenoise::Impl::clear_loopback_queue()
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    while (!m_loopback_queue.empty())
    {
        HailoMediaLibraryBufferPtr buffer = m_loopback_queue.front();
        m_loopback_queue.pop();
    }
    m_loopback_condvar.notify_one();
}

// Thread queue controls

void MediaLibraryPostIspDenoise::Impl::queue_inference_callback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
    m_inference_callback_condvar.wait(lock, [this] { return m_inference_callback_queue.size() < m_queue_size; });
    m_inference_callback_queue.push(buffer);
    m_inference_callback_condvar.notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryPostIspDenoise::Impl::dequeue_inference_callback_buffer()
{
    std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
    m_inference_callback_condvar.wait(lock, [this] { return !m_inference_callback_queue.empty() || m_flushing; });
    if (m_inference_callback_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_inference_callback_queue.front();
    m_inference_callback_queue.pop();
    m_inference_callback_condvar.notify_one();
    return buffer;
}

// Generic Queue Control

void MediaLibraryPostIspDenoise::Impl::queue_buffer(HailoMediaLibraryBufferPtr buffer,
                                                    std::queue<HailoMediaLibraryBufferPtr> &queue,
                                                    std::shared_ptr<std::mutex> mutex,
                                                    std::shared_ptr<std::condition_variable> condvar,
                                                    uint8_t queue_size)
{
    std::unique_lock<std::mutex> lock(*mutex);
    condvar->wait(lock, [queue, queue_size] { return queue.size() < queue_size; });
    queue.push(buffer);
    condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryPostIspDenoise::Impl::dequeue_buffer(
    std::queue<HailoMediaLibraryBufferPtr> &queue, std::shared_ptr<std::mutex> mutex,
    std::shared_ptr<std::condition_variable> condvar)
{
    std::unique_lock<std::mutex> lock(*mutex);
    condvar->wait(lock, [queue, this] { return !queue.empty() || m_flushing; });
    if (queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = queue.front();
    queue.pop();
    condvar->notify_one();
    return buffer;
}

void MediaLibraryPostIspDenoise::Impl::clear_queue(std::queue<HailoMediaLibraryBufferPtr> &queue,
                                                   std::shared_ptr<std::mutex> mutex,
                                                   std::shared_ptr<std::condition_variable> condvar)
{
    std::unique_lock<std::mutex> lock(*mutex);
    while (!queue.empty())
    {
        HailoMediaLibraryBufferPtr buffer = queue.front();
        queue.pop();
    }
    condvar->notify_one();
}
