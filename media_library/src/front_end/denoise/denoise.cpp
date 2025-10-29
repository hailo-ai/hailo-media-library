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
#include "denoise_common.hpp"
#include "snapshot.hpp"
#include "buffer_pool.hpp"
#include "config_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"
#include "hailo_media_library_perfetto.hpp"

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

MediaLibraryDenoise::MediaLibraryDenoise()
    : m_denoise_config_manager(ConfigSchema::CONFIG_SCHEMA_DENOISE),
      m_frontend_config_manager(ConfigSchema::CONFIG_SCHEMA_FRONTEND),
      m_hailort_config_manager(ConfigSchema::CONFIG_SCHEMA_HAILORT)
{
}

MediaLibraryDenoise::~MediaLibraryDenoise()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibraryDenoise - destructor");
    m_flushing = true;
    m_inference_callback_condvar.notify_one();
    m_loopback_condvar.notify_one();
    m_timestamp_condvar.notify_one();
    if (m_inference_callback_thread.joinable())
        m_inference_callback_thread.join();
    clear_loopback_queue();
    clear_timestamp_queue();

    // Free the startup buffer if it exists
    m_startup_buffer = nullptr;
}

media_library_return MediaLibraryDenoise::configure(const std::string &config_string)
{
    denoise_config_t denoise_configs;
    frontend_config_t frontend_config;
    hailort_t hailort_configs;
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring denoise Decoding json string");
    media_library_return hailort_status =
        m_hailort_config_manager.config_string_to_struct<hailort_t>(config_string, hailort_configs);
    if (hailort_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode Hailort config from json string: {}", config_string);
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return denoise_status =
        m_denoise_config_manager.config_string_to_struct<denoise_config_t>(config_string, denoise_configs);
    if (denoise_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode denoise config from json string: {}", config_string);
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return frontend_status =
        m_frontend_config_manager.config_string_to_struct<frontend_config_t>(config_string, frontend_config);
    if (frontend_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to decode frontend config from json string: {}", config_string);
        return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return configure(denoise_configs, hailort_configs, frontend_config.input_config);
}

media_library_return MediaLibraryDenoise::configure(const denoise_config_t &denoise_configs,
                                                    const hailort_t &hailort_configs,
                                                    const input_video_config_t &input_video_configs)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Configuring denoise");
    std::unique_lock<std::shared_mutex> lock(rw_lock);

    bool enabled_changed = enable_changed(denoise_configs);
    LOGGER__MODULE__INFO(MODULE_NAME, "NOTE: Loopback limit configurations are only applied when denoise is enabled.");

    if (!enabled_changed && !denoise_configs.enabled)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Denoise Remains disabled, skipping configuration");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    if (network_changed(denoise_configs, hailort_configs))
    {
        if (!m_hailort_denoise->set_config(denoise_configs, hailort_configs.device_id, HAILORT_SCHEDULER_THRESHOLD,
                                           HAILORT_SCHEDULER_TIMEOUT, HAILORT_SCHEDULER_BATCH_SIZE))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init hailort");
            return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // check if enabling
    if (enabled(denoise_configs))
    {
        media_library_return ret = create_and_initialize_buffer_pools(input_video_configs);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to allocate denoise buffer pool");
            return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
        }
        m_loop_counter = 0;
        m_loopback_batch_counter = 0;
        m_loopback_limit = denoise_configs.loopback_count;
        m_flushing = false;
        m_inference_callback_thread = std::thread(&MediaLibraryDenoise::inference_callback_thread, this);
    }

    // check if disabling
    if (disabled(denoise_configs))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Denoise disable requested, starting flushing");
        m_flushing = true;

        m_inference_callback_condvar.notify_all();

        if (m_inference_callback_thread.joinable())
            m_inference_callback_thread.join();

        m_loopback_condvar.notify_all();
        clear_loopback_queue();
        m_timestamp_condvar.notify_all();
        clear_timestamp_queue();

        if (close_buffer_pools() != MEDIA_LIBRARY_SUCCESS)
        {
            return media_library_return::MEDIA_LIBRARY_ERROR;
        }
    }

    // Call observing callbacks in case configuration changed
    for (auto &callbacks : m_callbacks)
    {
        if (enabled_changed && callbacks.on_enable_changed)
            callbacks.on_enable_changed(enabled(denoise_configs));
        if (enabled_changed && callbacks.send_event)
        {
            callbacks.send_event(enabled(denoise_configs));
        }
    }

    m_denoise_configs = denoise_configs;
    m_hailort_configs = hailort_configs;
    m_input_config = input_video_configs;
    m_sensor_index = input_video_configs.sensor_index;
    m_configured = true;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDenoise::stamp_time_and_log_fps(timespec &start_handle)
{
    struct timespec end_handle;
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "denoising frame took {} milliseconds ({} fps)", ms, framerate);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("denoise latency (ms)", ms, DENOISE_TRACK);
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
media_library_return MediaLibraryDenoise::perform_initial_batch(HailoMediaLibraryBufferPtr input_buffer,
                                                                HailoMediaLibraryBufferPtr output_buffer)
{
    if (!process_inference(input_buffer, input_buffer, output_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to process denoise, during initial batch");
        return media_library_return::MEDIA_LIBRARY_ERROR;
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

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

/**
 * Performs subsequent batches of de-noising
 *
 * @param input_buffer The input buffer containing the data to be denoised.
 * @param output_buffer The output buffer to store the denoised data.
 * @return The status of the denoising operation. Returns MEDIA_LIBRARY_SUCCESS if successful, MEDIA_LIBRARY_ERROR
 * otherwise.
 */
media_library_return MediaLibraryDenoise::perform_subsequent_batches(HailoMediaLibraryBufferPtr input_buffer,
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
            return media_library_return::MEDIA_LIBRARY_SUCCESS;
        }
        LOGGER__MODULE__ERROR(MODULE_NAME, "loopback buffer is null");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    if (!process_inference(input_buffer, loopback_buffer, output_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to process denoise");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Perform denoise
 * perform denoise on
 * the NN core
 *
 * @param[in] input_buffer - pointer to the input frame
 * @param[out] output_buffer - dewarp output buffer
 */
media_library_return MediaLibraryDenoise::perform_denoise(HailoMediaLibraryBufferPtr input_buffer,
                                                          HailoMediaLibraryBufferPtr output_buffer)
{
    // Acquire buffer for denoise output
    if (acquire_output_buffer(output_buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for denoise output");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    // check null for input and output buffer
    if (input_buffer == nullptr || output_buffer == nullptr)
    {
        // log: input or output buffer is null
        LOGGER__MODULE__ERROR(MODULE_NAME, "input or output buffer is null");
        return media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_loop_counter < m_loopback_limit)
    {
        return perform_initial_batch(input_buffer, output_buffer);
    }

    return perform_subsequent_batches(input_buffer, output_buffer);
}

media_library_return MediaLibraryDenoise::handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                                       HailoMediaLibraryBufferPtr output_frame)
{
    if (!is_enabled())
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Denoise is disabled - skipping handle_frame");
        return media_library_return::MEDIA_LIBRARY_UNINITIALIZED;
    }

    std::unique_lock<std::shared_mutex> lock(rw_lock);

    if (!currently_enabled())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Denoise is disabled, skipping denoise [handle_frame]");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    // Stamp start time and queue for retrieval after inference
    struct timespec start_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);
    queue_timestamp_buffer(start_handle);

    // Denoise
    copy_meta(input_frame, output_frame);
    media_library_return media_lib_ret = perform_denoise(input_frame, output_frame);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
        return media_lib_ret;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

denoise_config_t MediaLibraryDenoise::get_denoise_configs()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_denoise_configs;
}

hailort_t MediaLibraryDenoise::get_hailort_configs()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return m_hailort_configs;
}

bool MediaLibraryDenoise::is_enabled()
{
    std::unique_lock<std::shared_mutex> lock(rw_lock);
    return currently_enabled();
}

media_library_return MediaLibraryDenoise::observe(const MediaLibraryDenoise::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDenoise::inference_callback_thread()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
        if (m_flushing && !m_hailort_denoise->has_pending_jobs() && m_inference_callback_queue.empty())
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "Inference callback thread is exiting");
            return;
        }
        lock.unlock();
        HailoMediaLibraryBufferPtr output_buffer = dequeue_inference_callback_buffer();
        if (output_buffer == nullptr)
        {
            continue;
        }

        // This is when we push the output buffer, so stamp now for latency measurement
        auto timestamp_opt = dequeue_timestamp_buffer();
        if (timestamp_opt.has_value())
        {
            stamp_time_and_log_fps(timestamp_opt.value());
        }

        for (auto &callbacks : m_callbacks)
        {
            if (callbacks.on_buffer_ready)
            {
                callbacks.on_buffer_ready(output_buffer);
            }
        }

        if (output_buffer->owner && output_buffer->owner->get_format() == HAILO_FORMAT_NV12)
        {
            SnapshotManager::get_instance().take_snapshot("post_isp_denoise", output_buffer);
        }
    }
}

void MediaLibraryDenoise::inference_callback(HailoMediaLibraryBufferPtr output_buffer)
{
    queue_inference_callback_buffer(output_buffer);
}

// Loopback queue controls

void MediaLibraryDenoise::queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    m_loopback_condvar.wait(lock, [this] { return m_loopback_queue.size() < m_queue_size; });
    m_loopback_queue.push(buffer);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("loopback queue", m_loopback_queue.size(), DENOISE_TRACK);
    m_loopback_condvar.notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::dequeue_loopback_buffer()
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    m_loopback_condvar.wait(lock, [this] { return !m_loopback_queue.empty() || m_flushing; });
    if (m_loopback_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_loopback_queue.front();
    m_loopback_queue.pop();
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("loopback queue", m_loopback_queue.size(), DENOISE_TRACK);
    m_loopback_condvar.notify_one();
    return buffer;
}

void MediaLibraryDenoise::clear_loopback_queue()
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

void MediaLibraryDenoise::queue_inference_callback_buffer(HailoMediaLibraryBufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
    m_inference_callback_condvar.wait(lock, [this] { return m_inference_callback_queue.size() < m_queue_size; });
    m_inference_callback_queue.push(buffer);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("inference callback queue", m_inference_callback_queue.size(), DENOISE_TRACK);
    m_inference_callback_condvar.notify_one();
}

HailoMediaLibraryBufferPtr MediaLibraryDenoise::dequeue_inference_callback_buffer()
{
    std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
    m_inference_callback_condvar.wait(lock, [this] { return !m_inference_callback_queue.empty() || m_flushing; });
    if (m_inference_callback_queue.empty())
    {
        return nullptr;
    }
    HailoMediaLibraryBufferPtr buffer = m_inference_callback_queue.front();
    m_inference_callback_queue.pop();
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("inference callback queue", m_inference_callback_queue.size(), DENOISE_TRACK);
    m_inference_callback_condvar.notify_one();
    return buffer;
}

// Timestamp queue controls

void MediaLibraryDenoise::queue_timestamp_buffer(timespec start_handle)
{
    std::unique_lock<std::mutex> lock(m_timestamp_mutex);
    m_timestamp_condvar.wait(lock, [this] { return m_timestamp_queue.size() < m_timestamp_queue_size; });
    m_timestamp_queue.push(start_handle);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("timestamp queue", m_timestamp_queue.size(), DENOISE_TRACK);
    m_timestamp_condvar.notify_one();
}

std::optional<timespec> MediaLibraryDenoise::dequeue_timestamp_buffer()
{
    std::unique_lock<std::mutex> lock(m_timestamp_mutex);
    m_timestamp_condvar.wait(lock, [this] { return !m_timestamp_queue.empty() || m_flushing; });
    if (m_timestamp_queue.empty())
    {
        return std::nullopt;
    }
    timespec time_handle = m_timestamp_queue.front();
    m_timestamp_queue.pop();
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("timestamp queue", m_timestamp_queue.size(), DENOISE_TRACK);
    m_timestamp_condvar.notify_one();
    return time_handle;
}

void MediaLibraryDenoise::clear_timestamp_queue()
{
    std::unique_lock<std::mutex> lock(m_timestamp_mutex);
    while (!m_timestamp_queue.empty())
    {
        timespec time_handle = m_timestamp_queue.front();
        (void)time_handle;
        m_timestamp_queue.pop();
    }
    m_timestamp_condvar.notify_one();
}
