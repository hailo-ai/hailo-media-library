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

#include "denoise.hpp"
#include "hailort_denoise.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "media_library_logger.hpp"
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

#define BPOOL_MAX_SIZE 10
#define Q_SIZE 4

class MediaLibraryDenoise::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> create();
    static tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> create(std::string config_string);
    // Constructor
    Impl(media_library_return &status);
    Impl(media_library_return &status, std::string config_string);
    // Destructor
    ~Impl();
    // Move constructor
    Impl(Impl &&) = delete;
    // Move assignment
    Impl &operator=(Impl &&) = delete;

    // Configure the denoise module with new json string
    media_library_return configure(std::string config_string);

    // Configure the denoise module with denoise_config_t and hailort_t object
    media_library_return configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs);

    // Perform pre-processing on the input frame and return the output frames
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame);

    // get the denoise configurations object
    denoise_config_t &get_denoise_configs();

    // get the hailort configurations object
    hailort_t &get_hailort_configs();

    // get the enabled config status
    bool is_enabled();

    // set the callbacks object
    media_library_return observe(const MediaLibraryDenoise::callbacks_t &callbacks);

private:
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // configuration manager
    std::shared_ptr<ConfigManager> m_denoise_config_manager;
    std::shared_ptr<ConfigManager> m_hailort_config_manager;
    std::vector<MediaLibraryDenoise::callbacks_t> m_callbacks;
    // output buffer pool
    MediaLibraryBufferPoolPtr m_output_buffer_pool;
    // operation configurations
    denoise_config_t m_denoise_configs;
    hailort_t m_hailort_configs;
    // configuration mutex
    std::shared_mutex rw_lock;
    // HRT module
    HailortAsyncDenoisePtr m_hailort_denoise;
    // loopback controls
    uint8_t m_queue_size;
    uint8_t m_loop_counter;
    uint8_t m_initial_batch_callback_counter;
    uint8_t m_loopback_batch_counter;
    uint8_t m_loopback_limit;
    bool m_flushing;
    std::unique_ptr<std::condition_variable> m_loopback_condvar;
    std::shared_ptr<std::mutex> m_loopback_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_loopback_queue;
    std::unique_ptr<std::condition_variable> m_staging_condvar;
    std::shared_ptr<std::mutex> m_staging_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_staging_queue;
    // callback controls
    std::unique_ptr<std::condition_variable> m_inference_callback_condvar;
    std::shared_ptr<std::mutex> m_inference_callback_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_inference_callback_queue;

    media_library_return reconfigure();
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_configurations(denoise_config_t &denoise_configs, hailort_t &hailort_configs);
    media_library_return decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs, std::string config_string);
    media_library_return perform_denoise(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer);
    media_library_return perform_initial_batch(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer);
    media_library_return perform_subsequent_batches(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer);
    void set_default_members();
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void inference_callback(HailoMediaLibraryBufferPtr output_buffer);
    void queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_loopback_buffer();
    void clear_loopback_queue();
    void queue_staging_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_staging_buffer();
    void clear_staging_queue();
    void queue_buffer(HailoMediaLibraryBufferPtr buffer, std::queue<HailoMediaLibraryBufferPtr> &queue, std::shared_ptr<std::mutex> mutex, std::shared_ptr<std::condition_variable> condvar, uint8_t queue_size);
    HailoMediaLibraryBufferPtr dequeue_buffer(std::queue<HailoMediaLibraryBufferPtr> &queue, std::shared_ptr<std::mutex> mutex, std::shared_ptr<std::condition_variable> condvar);
    void clear_queue(std::queue<HailoMediaLibraryBufferPtr> &queue, std::shared_ptr<std::mutex> mutex, std::shared_ptr<std::condition_variable> condvar);
    void inference_callback_thread();
    std::thread m_inference_callback_thread;
    void queue_inference_callback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_inference_callback_buffer();
    void clear_inference_callback_queue();
};

//------------------------ MediaLibraryDenoise ------------------------
tl::expected<std::shared_ptr<MediaLibraryDenoise>, media_library_return> MediaLibraryDenoise::create()
{
    auto impl_expected = Impl::create();
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDenoise>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

tl::expected<std::shared_ptr<MediaLibraryDenoise>, media_library_return> MediaLibraryDenoise::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDenoise>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryDenoise::MediaLibraryDenoise(std::shared_ptr<MediaLibraryDenoise::Impl> impl) : m_impl(impl) {}

MediaLibraryDenoise::~MediaLibraryDenoise() = default;

media_library_return MediaLibraryDenoise::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryDenoise::configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs)
{
    return m_impl->configure(denoise_configs, hailort_configs);
}

media_library_return MediaLibraryDenoise::handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame)
{
    return m_impl->handle_frame(input_frame, output_frame);
}

denoise_config_t &MediaLibraryDenoise::get_denoise_configs()
{
    return m_impl->get_denoise_configs();
}

hailort_t &MediaLibraryDenoise::get_hailort_configs()
{
    return m_impl->get_hailort_configs();
}

bool MediaLibraryDenoise::is_enabled()
{
    return m_impl->is_enabled();
}

media_library_return MediaLibraryDenoise::observe(const MediaLibraryDenoise::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

//------------------------ MediaLibraryDenoise::Impl ------------------------
tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> MediaLibraryDenoise::Impl::create()
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDenoise::Impl> denoise = std::make_shared<MediaLibraryDenoise::Impl>(status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return denoise;
}

tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> MediaLibraryDenoise::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDenoise::Impl> denoise = std::make_shared<MediaLibraryDenoise::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return denoise;
}

void MediaLibraryDenoise::Impl::set_default_members()
{
    m_configured = false;
    m_denoise_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_DENOISE);
    m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    m_hailort_denoise = std::make_shared<HailortAsyncDenoise>([this](HailoMediaLibraryBufferPtr output_buffer)
                                                              { inference_callback(output_buffer); });

    m_loopback_limit = 1;
    m_loop_counter = 0;
    m_initial_batch_callback_counter = 0;
    m_loopback_batch_counter = 0;
    m_flushing = false;
    m_queue_size = Q_SIZE;
    m_loopback_mutex = std::make_shared<std::mutex>();
    m_loopback_condvar = std::make_unique<std::condition_variable>();
    m_loopback_queue = std::queue<HailoMediaLibraryBufferPtr>();
    m_staging_mutex = std::make_shared<std::mutex>();
    m_staging_condvar = std::make_unique<std::condition_variable>();
    m_staging_queue = std::queue<HailoMediaLibraryBufferPtr>();
    m_inference_callback_mutex = std::make_shared<std::mutex>();
    m_inference_callback_condvar = std::make_unique<std::condition_variable>();
    m_inference_callback_queue = std::queue<HailoMediaLibraryBufferPtr>();
}

MediaLibraryDenoise::Impl::Impl(media_library_return &status)
{
    set_default_members();

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDenoise::Impl::Impl(media_library_return &status, std::string config_string)
{
    set_default_members();

    if (decode_config_json_string(m_denoise_configs, m_hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string");
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    if (configure(m_denoise_configs, m_hailort_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to configure denoise");
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return;
    }

    m_configured = true;

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDenoise::Impl::~Impl()
{
    LOGGER__DEBUG("Denoise - destructor");
    m_flushing = true;
    m_inference_callback_condvar->notify_one();
    m_loopback_condvar->notify_one();
    m_staging_condvar->notify_one();
    if (m_inference_callback_thread.joinable())
        m_inference_callback_thread.join();
    m_hailort_denoise.reset();
    clear_inference_callback_queue();
    clear_loopback_queue();
    clear_staging_queue();
}

media_library_return MediaLibraryDenoise::Impl::decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs, std::string config_string)
{
    media_library_return hailort_status = m_hailort_config_manager->config_string_to_struct<hailort_t>(config_string, hailort_configs);
    if (hailort_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode Hailort config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return denoise_status = m_denoise_config_manager->config_string_to_struct<denoise_config_t>(config_string, denoise_configs);
    if (denoise_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode denoise config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::Impl::configure(std::string config_string)
{
    denoise_config_t denoise_configs;
    hailort_t hailort_configs;
    LOGGER__INFO("Configuring denoise Decoding json string");
    if (decode_config_json_string(denoise_configs, hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(denoise_configs, hailort_configs);
}

media_library_return MediaLibraryDenoise::Impl::validate_configurations(denoise_config_t &denoise_configs, hailort_t &hailort_configs)
{
    // TODO: add validation
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::Impl::configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs)
{
    LOGGER__INFO("Configuring denoise");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    if (validate_configurations(denoise_configs, hailort_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to validate configurations");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto prev_enabled = m_denoise_configs.enabled;
    m_denoise_configs = denoise_configs;
    m_hailort_configs = hailort_configs;
    bool enabled_changed = m_denoise_configs.enabled != prev_enabled;
    LOGGER__INFO("NOTE: Loopback limit configurations are only applied when denoise is enabled.");

    if (prev_enabled == false && m_denoise_configs.enabled == false)
    {
        LOGGER__INFO("Denoise Remains disabled, skipping configuration");
    }

    // on first configure
    if (!m_configured)
    {
        int status = m_hailort_denoise->init(m_denoise_configs.network_config, m_hailort_configs.device_id, 1, 1000, 2);
        if (status != 0)
        {
            LOGGER__ERROR("Failed to init hailort");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        // Create and initialize buffer pools
        media_library_return ret = create_and_initialize_buffer_pools();
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to allocate denoise buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
    }

    // check if enabling
    if (m_denoise_configs.enabled && (enabled_changed || !m_configured))
    {
        m_loop_counter = 0;
        m_initial_batch_callback_counter = 0;
        m_loopback_batch_counter = 0;
        m_loopback_limit = m_denoise_configs.loopback_count;
        m_flushing = false;
        m_inference_callback_thread = std::thread(&MediaLibraryDenoise::Impl::inference_callback_thread, this);
    }

    // check if disabling
    if (!m_denoise_configs.enabled && enabled_changed)
    {
        m_flushing = true;
        // notify all queues that we are flushing
        m_inference_callback_condvar->notify_one();
        m_loopback_condvar->notify_one();
        m_staging_condvar->notify_one();
        if (m_inference_callback_thread.joinable())
            m_inference_callback_thread.join();
        clear_inference_callback_queue();
        clear_loopback_queue();
        clear_staging_queue();
    }

    // Call observing callbacks in case configuration changed
    for (auto &callbacks : m_callbacks)
    {
        if ((!m_configured || enabled_changed) && callbacks.on_enable_changed)
            callbacks.on_enable_changed(m_denoise_configs.enabled);
    }
    m_configured = true;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::Impl::create_and_initialize_buffer_pools()
{
    // Only support 4k for now
    uint width, height;
    width = 3840;
    height = 2160;
    std::string name = "denoise_output";
    if (m_output_buffer_pool)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    // Create output buffer pool
    LOGGER__DEBUG("Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {}", name, width, height, BPOOL_MAX_SIZE);
    m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, height, DSP_IMAGE_FORMAT_NV12, BPOOL_MAX_SIZE, CMA, name);
    if (m_output_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init buffer pool");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDenoise::Impl::stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle)
{
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__DEBUG("denoise handle_frame took {} milliseconds ({} fps)", ms, framerate);
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
 * @return The status of the denoising operation. Returns MEDIA_LIBRARY_SUCCESS if successful, MEDIA_LIBRARY_ERROR otherwise.
 */
media_library_return MediaLibraryDenoise::Impl::perform_initial_batch(
    HailoMediaLibraryBufferPtr input_buffer,
    HailoMediaLibraryBufferPtr output_buffer)
{
    int ret = m_hailort_denoise->process(input_buffer, input_buffer, output_buffer);

    if (ret != 0)
    {
        LOGGER__ERROR("Failed to process denoise, during initial batch");
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
 * @return The status of the denoising operation. Returns MEDIA_LIBRARY_SUCCESS if successful, MEDIA_LIBRARY_ERROR otherwise.
 */
media_library_return MediaLibraryDenoise::Impl::perform_subsequent_batches(
    HailoMediaLibraryBufferPtr input_buffer,
    HailoMediaLibraryBufferPtr output_buffer)
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
        LOGGER__ERROR("loopback buffer is null");
        return MEDIA_LIBRARY_ERROR;
    }

    queue_staging_buffer(loopback_buffer);
    int ret = m_hailort_denoise->process(input_buffer, loopback_buffer, output_buffer);

    if (ret != 0)
    {
        LOGGER__ERROR("Failed to process denoise");
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
media_library_return MediaLibraryDenoise::Impl::perform_denoise(
    HailoMediaLibraryBufferPtr input_buffer,
    HailoMediaLibraryBufferPtr output_buffer)
{
    // Acquire buffer for denoise output
    if (m_output_buffer_pool->acquire_buffer(output_buffer) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("failed to acquire buffer for denoise output");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // check null for input and output buffer
    if (input_buffer == nullptr || output_buffer == nullptr)
    {
        // log: input or output buffer is null
        LOGGER__ERROR("input or output buffer is null");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_loop_counter < m_loopback_limit)
    {
        return perform_initial_batch(input_buffer, output_buffer);

    }

    return perform_subsequent_batches(input_buffer, output_buffer);
}

media_library_return MediaLibraryDenoise::Impl::handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame)
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    // Denoise
    media_library_return media_lib_ret = perform_denoise(input_frame, output_frame);

    output_frame->isp_ae_fps = input_frame->isp_ae_fps;
    output_frame->isp_ae_converged = input_frame->isp_ae_converged;
    output_frame->isp_ae_average_luma = input_frame->isp_ae_average_luma;
    output_frame->isp_ae_integration_time = input_frame->isp_ae_integration_time;
    output_frame->isp_timestamp_ns = input_frame->isp_timestamp_ns;
    output_frame->pts = input_frame->pts;

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    stamp_time_and_log_fps(start_handle, end_handle);

    return MEDIA_LIBRARY_SUCCESS;
}

denoise_config_t &MediaLibraryDenoise::Impl::get_denoise_configs()
{
    return m_denoise_configs;
}

hailort_t &MediaLibraryDenoise::Impl::get_hailort_configs()
{
    return m_hailort_configs;
}

bool MediaLibraryDenoise::Impl::is_enabled()
{
    return m_denoise_configs.enabled;
}

media_library_return MediaLibraryDenoise::Impl::observe(const MediaLibraryDenoise::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDenoise::Impl::inference_callback_thread()
{
    while (!m_flushing)
    {
        HailoMediaLibraryBufferPtr output_buffer = dequeue_inference_callback_buffer();
        if (output_buffer == nullptr)
        {
            if (m_flushing)
            {
                return;
            }
            LOGGER__ERROR("denoise.cpp inference_callback_thread output_buffer is null and not flushing");
            return;
        }
        // Call observing callbacks in case configuration changed
        if (m_initial_batch_callback_counter >= m_loopback_limit)
        {
            HailoMediaLibraryBufferPtr staging_buffer = dequeue_staging_buffer();
        }
        else
        {
            m_initial_batch_callback_counter++;
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

void MediaLibraryDenoise::Impl::inference_callback(HailoMediaLibraryBufferPtr output_buffer)
{
    if (!m_flushing)
    {
        queue_inference_callback_buffer(output_buffer);
    }
}

// Loopback queue controls

void MediaLibraryDenoise::Impl::queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_loopback_mutex));
    m_loopback_condvar->wait(lock, [this]
                             { return m_loopback_queue.size() < m_queue_size; });
    m_loopback_queue.push(buffer);
    m_loopback_condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::Impl::dequeue_loopback_buffer()
{
    std::unique_lock<std::mutex> lock(*(m_loopback_mutex));
    m_loopback_condvar->wait(lock, [this]
                             { return !m_loopback_queue.empty() || m_flushing; });
    if (m_loopback_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_loopback_queue.front();
    m_loopback_queue.pop();
    m_loopback_condvar->notify_one();
    return buffer;
}

void MediaLibraryDenoise::Impl::clear_loopback_queue()
{
    std::unique_lock<std::mutex> lock(*(m_loopback_mutex));
    while (!m_loopback_queue.empty())
    {
        HailoMediaLibraryBufferPtr buffer = m_loopback_queue.front();
        m_loopback_queue.pop();
    }
    m_loopback_condvar->notify_one();
}

// Staging queue controls

void MediaLibraryDenoise::Impl::queue_staging_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_staging_mutex));
    m_staging_condvar->wait(lock, [this]
                            { return m_staging_queue.size() < m_queue_size; });
    m_staging_queue.push(buffer);
    m_staging_condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::Impl::dequeue_staging_buffer()
{
    std::unique_lock<std::mutex> lock(*(m_staging_mutex));
    m_staging_condvar->wait(lock, [this]
                            { return !m_staging_queue.empty() || m_flushing; });
    if (m_staging_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_staging_queue.front();
    m_staging_queue.pop();
    m_staging_condvar->notify_one();
    return buffer;
}

void MediaLibraryDenoise::Impl::clear_staging_queue()
{
    std::unique_lock<std::mutex> lock(*(m_staging_mutex));
    while (!m_staging_queue.empty())
    {
        HailoMediaLibraryBufferPtr buffer = m_staging_queue.front();
        m_staging_queue.pop();
    }
    m_staging_condvar->notify_one();
}

// Thread queue controls

void MediaLibraryDenoise::Impl::queue_inference_callback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_inference_callback_mutex));
    m_inference_callback_condvar->wait(lock, [this]
                                       { return m_inference_callback_queue.size() < m_queue_size; });
    m_inference_callback_queue.push(buffer);
    m_inference_callback_condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::Impl::dequeue_inference_callback_buffer()
{
    std::unique_lock<std::mutex> lock(*(m_inference_callback_mutex));
    m_inference_callback_condvar->wait(lock, [this]
                                       { return !m_inference_callback_queue.empty() || m_flushing; });
    if (m_inference_callback_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_inference_callback_queue.front();
    m_inference_callback_queue.pop();
    m_inference_callback_condvar->notify_one();
    return buffer;
}

void MediaLibraryDenoise::Impl::clear_inference_callback_queue()
{
    std::unique_lock<std::mutex> lock(*(m_inference_callback_mutex));
    while (!m_inference_callback_queue.empty())
    {
        HailoMediaLibraryBufferPtr buffer = m_inference_callback_queue.front();
        m_inference_callback_queue.pop();
    }
    m_inference_callback_condvar->notify_one();
}

// Generic Queue Control

void MediaLibraryDenoise::Impl::queue_buffer(HailoMediaLibraryBufferPtr buffer,
                                             std::queue<HailoMediaLibraryBufferPtr> &queue,
                                             std::shared_ptr<std::mutex> mutex,
                                             std::shared_ptr<std::condition_variable> condvar,
                                             uint8_t queue_size)
{
    std::unique_lock<std::mutex> lock(*mutex);
    condvar->wait(lock, [queue, queue_size]
                  { return queue.size() < queue_size; });
    queue.push(buffer);
    condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::Impl::dequeue_buffer(std::queue<HailoMediaLibraryBufferPtr> &queue,
                                                                     std::shared_ptr<std::mutex> mutex,
                                                                     std::shared_ptr<std::condition_variable> condvar)
{
    std::unique_lock<std::mutex> lock(*mutex);
    condvar->wait(lock, [queue, this]
                  { return !queue.empty() || m_flushing; });
    if (queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = queue.front();
    queue.pop();
    condvar->notify_one();
    return buffer;
}

void MediaLibraryDenoise::Impl::clear_queue(std::queue<HailoMediaLibraryBufferPtr> &queue,
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
