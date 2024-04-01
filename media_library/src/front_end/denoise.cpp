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
#include <condition_variable>

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
    bool m_flushing;
    std::unique_ptr<std::condition_variable> m_loopback_condvar;
    std::shared_ptr<std::mutex> m_loopback_mutex;
    uint8_t m_queue_size;
    uint8_t m_loop_counter;
    std::queue<HailoMediaLibraryBufferPtr> m_loopback_queue;
    std::unique_ptr<std::condition_variable> m_staging_condvar;
    std::shared_ptr<std::mutex> m_staging_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_staging_queue;

    media_library_return reconfigure();
    media_library_return create_and_initialize_buffer_pools();
    media_library_return validate_configurations(denoise_config_t &denoise_configs, hailort_t &hailort_configs);
    media_library_return decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs, std::string config_string);
    media_library_return perform_denoise(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer);
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    void inference_callback(HailoMediaLibraryBufferPtr output_buffer);
    void queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_loopback_buffer();
    void clear_loopback_queue();
    void queue_staging_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_staging_buffer();
    void clear_staging_queue();
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

MediaLibraryDenoise::Impl::Impl(media_library_return &status)
{
    m_configured = false;
    m_denoise_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_DENOISE);
    m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    m_hailort_denoise = std::make_shared<HailortAsyncDenoise>([this](HailoMediaLibraryBufferPtr output_buffer)
                                                              { inference_callback(output_buffer); });

    m_loop_counter = 0;
    m_flushing = false;
    m_queue_size = 5;
    m_loopback_mutex = std::make_shared<std::mutex>();
    m_loopback_condvar = std::make_unique<std::condition_variable>();
    m_loopback_queue = std::queue<HailoMediaLibraryBufferPtr>();
    m_staging_mutex = std::make_shared<std::mutex>();
    m_staging_condvar = std::make_unique<std::condition_variable>();
    m_staging_queue = std::queue<HailoMediaLibraryBufferPtr>();

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDenoise::Impl::Impl(media_library_return &status, std::string config_string)
{
    m_configured = false;
    m_denoise_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_DENOISE);
    m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    m_hailort_denoise = std::make_shared<HailortAsyncDenoise>([this](HailoMediaLibraryBufferPtr output_buffer)
                                                              { inference_callback(output_buffer); });

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

    m_loop_counter = 0;
    m_flushing = false;
    m_queue_size = 5;
    m_loopback_mutex = std::make_shared<std::mutex>();
    m_loopback_condvar = std::make_unique<std::condition_variable>();
    m_loopback_queue = std::queue<HailoMediaLibraryBufferPtr>();
    m_staging_mutex = std::make_shared<std::mutex>();
    m_staging_condvar = std::make_unique<std::condition_variable>();
    m_staging_queue = std::queue<HailoMediaLibraryBufferPtr>();

    m_configured = true;

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDenoise::Impl::~Impl()
{
    LOGGER__DEBUG("Denoise - destructor");
    m_flushing = true;
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
    // LOGGER__ERROR("Configuring denoise");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    if (validate_configurations(denoise_configs, hailort_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to validate configurations");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    // LOGGER__ERROR("----> in configure");
    auto prev_enabled = m_denoise_configs.enabled;
    // LOGGER__ERROR("----> prev_enabled: {}", prev_enabled);
    m_denoise_configs = denoise_configs;
    m_hailort_configs = hailort_configs;
    // LOGGER__ERROR("----> m_denoise_configs.enabled: {}", m_denoise_configs.enabled);
    bool enabled_changed = m_denoise_configs.enabled != prev_enabled;

    if (m_denoise_configs.enabled && (enabled_changed || !m_configured))
    {
        // LOGGER__ERROR("----> calling init fro hailort_denoise");
        // auto time_start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        int status = m_hailort_denoise->init(m_denoise_configs.network_config, m_hailort_configs.device_id, 1, 1000);
        if (status != 0)
        {
            LOGGER__ERROR("Failed to init hailort");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        // auto time_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        // float init_fps = 1000000 / (time_end - time_start);
        // LOGGER__ERROR("hailo_denoise: calling init took {} us, fps: {}", time_end - time_start, init_fps);
    
        // Create and initialize buffer pools
        media_library_return ret = create_and_initialize_buffer_pools();
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Failed to allocate denoise buffer pool");
            return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
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
    uint width, height, bpool_max_size;
    width = 3840;
    height = 2160;
    bpool_max_size = 5;
    std::string name = "denoise_output";

    // Create output buffer pool
    LOGGER__DEBUG("Creating buffer pool named {} for output resolution: width {} height {} in buffers size of {}", name, width, height, bpool_max_size);
    m_output_buffer_pool = std::make_shared<MediaLibraryBufferPool>(width, height, DSP_IMAGE_FORMAT_NV12, bpool_max_size, CMA, name);
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
 * @brief Perform denosie
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

    // Acquire buffer for dewarp output
    if (m_output_buffer_pool->acquire_buffer(*output_buffer.get()) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        // log: failed to acquire buffer for dewarp output
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // check null for input and output buffer
    if (input_buffer == nullptr || output_buffer == nullptr)
    {
        // log: input or output buffer is null
        LOGGER__ERROR("input or output buffer is null");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    // Perform dewarp
    int ret = -1;
    if (m_loop_counter < 1)
    {
        ret = m_hailort_denoise->process(input_buffer, input_buffer, output_buffer);
        m_loop_counter++;
    }
    else
    {
        HailoMediaLibraryBufferPtr loopback_buffer = dequeue_loopback_buffer();
        if (loopback_buffer == nullptr)
        {
            LOGGER__ERROR("loopback buffer is null");
            return MEDIA_LIBRARY_ERROR;
        }
        else
        {
            // LOGGER__ERROR("loopback buffer is not null, index: {}, plane 0 refcount: {}", loopback_buffer->buffer_index,loopback_buffer->refcount(0));
        }
        queue_staging_buffer(loopback_buffer);
        // LOGGER__ERROR("calling process");
        ret = m_hailort_denoise->process(input_buffer, loopback_buffer, output_buffer);

        // std::thread process_t([this, input_buffer, loopback_buffer, output_buffer] {
        //     int ret = m_hailort_denoise->process(input_buffer, loopback_buffer, output_buffer);
        //     if (ret != 0)
        //     {
        //         // log: failed to perform denoise
        //         LOGGER__ERROR("Failed to process denoise");
        //     }
        // });
        // process_t.detach();
        // LOGGER__ERROR("finished process");
    }
    // LOGGER__ERROR("Called process, loopback_counter now {}", m_loop_counter);
    if (ret != 0)
    {
        // log: failed to perform denoise
        LOGGER__ERROR("Failed to process denoise");
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::Impl::handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame)
{
    std::shared_lock<std::shared_mutex> lock(rw_lock);

    // Stamp start time
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);

    // Denoise
    media_library_return media_lib_ret = perform_denoise(input_frame, output_frame);

    // Unref the input frame
    input_frame->decrease_ref_count();

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

void MediaLibraryDenoise::Impl::inference_callback(HailoMediaLibraryBufferPtr output_buffer)
{
    // LOGGER__ERROR("denoise.cpp inference_callback start");
    // Call observing callbacks in case configuration changed
    // LOGGER__ERROR("inference_callback, loopback_counter now {}", m_loop_counter);
    if (m_loop_counter > 1)
    {
        // LOGGER__ERROR("dequeuing staging buffer with loopback counter at {}", m_loop_counter);
        HailoMediaLibraryBufferPtr staging_buffer = dequeue_staging_buffer();
        staging_buffer->decrease_ref_count();
    }
    else
    {
        m_loop_counter++;
    }
    // double increase since dewarp/multi-resize decreases twice (once through medialib_buffer, once through gstbuffer)
    output_buffer->increase_ref_count();
    output_buffer->increase_ref_count();
    queue_loopback_buffer(output_buffer);
    for (auto &callbacks : m_callbacks)
    {
        if (callbacks.on_buffer_ready)
        {
            // LOGGER__ERROR("denoise.cpp inference_callback calling on_buffer_ready");
            callbacks.on_buffer_ready(output_buffer);
        }
    }
    // LOGGER__ERROR("denoise.cpp inference_callback end");
    // output_buffer->decrease_ref_count();
}

void MediaLibraryDenoise::Impl::queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_loopback_mutex));

    m_loopback_condvar->wait(lock, [this]
                          { // LOGGER__ERROR("queue_loopback_buffer wait");
                            return m_loopback_queue.size() < m_queue_size; });
    // LOGGER__ERROR("!!!queue_loopback_buffer");
    m_loopback_queue.push(buffer);
    m_loopback_condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::Impl::dequeue_loopback_buffer()
{
    std::unique_lock<std::mutex> lock(*(m_loopback_mutex));
    m_loopback_condvar->wait(lock, [this]
                          { // LOGGER__ERROR("dequeue_loopback_buffer wait");
                            return !m_loopback_queue.empty() || m_flushing; });
    // LOGGER__ERROR("!!!dequeue_loopback_buffer");  
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
        buffer->decrease_ref_count();
        buffer->decrease_ref_count();
    }
    m_loopback_condvar->notify_one();
}

void MediaLibraryDenoise::Impl::queue_staging_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_staging_mutex));
    m_staging_condvar->wait(lock, [this]
                          { // LOGGER__ERROR("queue_staging_buffer wait");
                            return m_staging_queue.size() < m_queue_size; });
    m_staging_queue.push(buffer);
    m_staging_condvar->notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::Impl::dequeue_staging_buffer()
{
    std::unique_lock<std::mutex> lock(*(m_staging_mutex));
    m_staging_condvar->wait(lock, [this]
                          { // LOGGER__ERROR("dequeue_staging_buffer wait");
                            return !m_staging_queue.empty() || m_flushing; });
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
        buffer->decrease_ref_count();
        buffer->decrease_ref_count();
    }
    m_staging_condvar->notify_one();
}