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
#include "snapshot.hpp"
#include "buffer_pool.hpp"
#include "config_parser.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include "media_library_utils.hpp"
#include "hailo_media_library_perfetto.hpp"
#include "perfetto_fps_tracer.hpp"

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

MediaLibraryDenoise::MediaLibraryDenoise() : m_fps_tracer("Denoise FPS"), m_initialized(false)
{
}

void join_inference_callback_thread(std::thread &inference_callback_thread)
{
    if (inference_callback_thread.joinable())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Waiting for inference callback thread to join");
        inference_callback_thread.join();
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Inference callback thread joined successfully");
    }
}

MediaLibraryDenoise::~MediaLibraryDenoise()
{
}

media_library_return MediaLibraryDenoise::init(const denoise_config_t &denoise_configs,
                                               const input_video_config_t &input_video_configs)
{
    if (m_initialized)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Initializing denoise buffer pools and threads");
    m_should_queue_dummy_loopback_buffer = true;
    prepare_hailort_instance(denoise_configs);
    // NOTE: buffer pools are allocated based on the hailort instance type
    media_library_return ret = create_and_initialize_buffer_pools(input_video_configs);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to allocate denoise buffer pool");
        return media_library_return::MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    m_initialized = true;
    m_loop_counter = 0;
    m_loopback_limit = denoise_configs.loopback_count;
    m_flushing = false;
    start_inference_callback_thread();
    LOGGER__MODULE__INFO(MODULE_NAME, "Denoise initialized successfully - loopback_limit: {}", m_loopback_limit);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::deinit()
{
    if (!m_initialized)
    {
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Deinitializing denoise - stopping threads and cleaning up resources");
    stop_inference_callback_thread();

    LOGGER__MODULE__TRACE(MODULE_NAME, "Closing buffer pools");
    if (free_buffer_pools() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to close buffer pools during deinitialization");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    LOGGER__MODULE__INFO(MODULE_NAME, "Denoise deinitialized successfully");
    m_initialized = false;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::configure(HailoMediaLibraryBufferPtr input_frame)
{
    const auto &denoise_configs = input_frame->get_attached_profile()->iq_settings.denoise;
    // NOTE: Loopback limit configurations are only applied when denoise is enabled.
    LOGGER__MODULE__TRACE(MODULE_NAME, "Configuring denoise - enabled: {}, bayer: {}, loopback_count: {}",
                          denoise_configs.enabled, denoise_configs.bayer, denoise_configs.loopback_count);

    bool enabled_changed = m_initialized != is_enabled(denoise_configs);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Configuration state - enabled_changed: {}, is_enabled: {}", enabled_changed,
                          is_enabled(denoise_configs));

    if (!enabled_changed && !denoise_configs.enabled)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Denoise Remains disabled, skipping configuration");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    const auto frontend_config = input_frame->get_attached_profile()->to_frontend_config();
    const auto &input_video_configs = frontend_config.input_config;
    const auto &hailort_configs = frontend_config.hailort_config;

    if (is_enabled(denoise_configs))
    {
        const bool network_changed = m_hailort_denoise->get_network_path(denoise_configs) !=
                                     m_hailort_denoise->get_network_path(m_denoise_configs);
        if (network_changed)
        {
            deinit();
        }
        if (init(denoise_configs, input_video_configs) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to initialize denoise");
            return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        if (!m_hailort_denoise->set_config(denoise_configs, hailort_configs.device_id, HAILORT_SCHEDULER_THRESHOLD,
                                           HAILORT_SCHEDULER_TIMEOUT, HAILORT_SCHEDULER_BATCH_SIZE))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init hailort with device_id: {}", hailort_configs.device_id);
            return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }
    else
    {
        deinit();
    }

    if (enabled_changed)
    {
        for (auto &callbacks : m_callbacks)
        {
            if (enabled_changed && callbacks.on_enable_changed)
                callbacks.on_enable_changed(is_enabled(denoise_configs));
        }
    }

    m_denoise_configs = denoise_configs;
    m_hailort_device_id = hailort_configs.device_id;
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDenoise::stamp_time_and_log_fps(timespec &start_handle)
{
    struct timespec end_handle;
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    long ms = (long)media_library_difftimespec_ms(end_handle, start_handle);
    uint framerate = 1000 / ms;
    LOGGER__MODULE__TRACE(MODULE_NAME, "denoising frame took {} milliseconds ({} fps)", ms, framerate);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("denoise latency (ms)", ms, DENOISE_TRACK, MEDIA_LIBRARY_CATEGORY);

    m_fps_tracer.record_frame();
}

/**
 * Performs subsequent batches of de-noising
 *
 * @param input_buffer The input buffer containing the data to be denoised.
 * @param output_buffer The output buffer to store the denoised data.
 * @return The status of the denoising operation. Returns MEDIA_LIBRARY_SUCCESS if successful, MEDIA_LIBRARY_ERROR
 * otherwise.
 */
media_library_return MediaLibraryDenoise::acquire_loopback_buffer(NetworkInferenceBindingsPtr bindings)
{
    auto loopback_buffers = dequeue_loopback_buffer();
    if (!loopback_buffers.has_value())
    {
        if (!m_flushing)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "dequeue_loopback_buffer failed.");
        }
        return loopback_buffers.error();
    }
    auto result = bind_loopback_buffers(std::move(bindings), loopback_buffers.value());
    if (result != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        if (!m_flushing)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "loopback buffer is not set");
        }
        return result;
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
media_library_return MediaLibraryDenoise::perform_denoise(NetworkInferenceBindingsPtr bindings)
{
    media_library_return result;
    if (acquire_output_buffer(bindings) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire buffer for denoise output");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    // Initialize loopback buffers with dummy buffers as optimization
    if (initialize_loopback_buffers(bindings->outputs) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "failed to initialize loopback buffers");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    // Early queuing optimization of loopback buffers
    queue_loopback_buffer(bindings->outputs);

    result = acquire_loopback_buffer(bindings);
    if (m_flushing && result == media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Flushing in progress - returning success with null loopback buffer");
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    else if (result != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "failed to acquire loopback buffer");
        return result;
    }

    if (!process_inference(std::move(bindings)))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to process denoise");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::handle_frame(HailoMediaLibraryBufferPtr input_frame)
{
    if (input_frame == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "input buffer is null");
        return media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (!is_enabled(input_frame->get_attached_profile()->iq_settings.denoise))
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Denoise is currently disabled - skipping denoise processing");
        return media_library_return::MEDIA_LIBRARY_UNINITIALIZED;
    }

    std::unique_lock<std::shared_mutex> lock(rw_lock);
    media_library_return media_lib_ret = configure(input_frame);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        return media_lib_ret;
    }

    HailoMediaLibraryBufferPtr output_frame = std::make_shared<hailo_media_library_buffer>();

    LOGGER__MODULE__TRACE(MODULE_NAME, "Processing frame - loop_counter: {}", m_loop_counter++);

    // Stamp start time and queue for retrieval after inference for latency measurement
    struct timespec start_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);
    queue_timestamp_buffer(start_handle);

    // Denoise
    copy_meta(input_frame, output_frame);

    auto bindings = create_bindings(input_frame, output_frame);
    media_lib_ret = perform_denoise(std::move(bindings));
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to perform denoise in handle_frame - result: {}",
                              static_cast<int>(media_lib_ret));
        return media_lib_ret;
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Frame processed successfully");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::observe(const MediaLibraryDenoise::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void MediaLibraryDenoise::inference_callback_thread()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Inference callback thread started");

    while (true)
    {
        std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
        if (m_flushing && !m_hailort_denoise->has_pending_jobs() && m_inference_callback_queue.empty())
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "Inference callback thread exiting - flushing complete");
            return;
        }
        lock.unlock();
        NetworkInferenceBindingsPtr bindings = dequeue_inference_callback_buffer();
        if (bindings == nullptr)
        {
            continue;
        }

        LOGGER__MODULE__TRACE(MODULE_NAME, "Processing inference callback - queue size: {}",
                              m_inference_callback_queue.size());

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
                auto output_buffer = get_output_buffer(bindings, get_denoised_output_index());
                callbacks.on_buffer_ready(output_buffer);
            }
        }

        HailoMediaLibraryBufferPtr output_buffer = get_output_buffer(bindings, get_denoised_output_index());
        if (output_buffer && output_buffer->owner && output_buffer->owner->get_format() == HAILO_FORMAT_NV12)
        {
            SnapshotManager::get_instance().take_snapshot("denoise", output_buffer);
        }

        LOGGER__MODULE__TRACE(MODULE_NAME, "Inference callback processed successfully");
    }
}

void MediaLibraryDenoise::inference_callback(NetworkInferenceBindingsPtr bindings)
{
    queue_inference_callback_buffer(bindings);
}

// Loopback queue controls

void MediaLibraryDenoise::queue_loopback_buffer(const TensorBindings &loopback_buffers)
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    m_loopback_condvar.wait(lock, [this] { return m_loopback_queue.size() < m_queue_size; });
    m_loopback_queue.push(loopback_buffers);
    LOGGER__MODULE__TRACE(MODULE_NAME, "Queued loopback buffer - queue size: {}", m_loopback_queue.size());
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("loopback queue", m_loopback_queue.size(), DENOISE_TRACK, MEDIA_LIBRARY_CATEGORY);
    m_loopback_condvar.notify_one();
}

tl::expected<TensorBindings, media_library_return> MediaLibraryDenoise::dequeue_loopback_buffer()
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    m_loopback_condvar.wait(lock, [this] { return !m_loopback_queue.empty() || m_flushing; });
    if (m_loopback_queue.empty())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Loopback queue is empty");
        return tl::unexpected(media_library_return::MEDIA_LIBRARY_BUFFER_NOT_FOUND);
    }
    auto loopback_buffers = m_loopback_queue.front();
    m_loopback_queue.pop();
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("loopback queue", m_loopback_queue.size(), DENOISE_TRACK, MEDIA_LIBRARY_CATEGORY);
    m_loopback_condvar.notify_one();
    return loopback_buffers;
}

void MediaLibraryDenoise::clear_loopback_queue()
{
    std::unique_lock<std::mutex> lock(m_loopback_mutex);
    while (!m_loopback_queue.empty())
    {
        m_loopback_queue.pop();
    }
    m_loopback_condvar.notify_one();
}

// Thread queue controls

void MediaLibraryDenoise::queue_inference_callback_buffer(NetworkInferenceBindingsPtr bindings)
{
    std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
    m_inference_callback_condvar.wait(lock, [this] { return m_inference_callback_queue.size() < m_queue_size; });
    m_inference_callback_queue.push(bindings);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("inference callback queue", m_inference_callback_queue.size(), DENOISE_TRACK,
                                      MEDIA_LIBRARY_CATEGORY);
    m_inference_callback_condvar.notify_one();
}

NetworkInferenceBindingsPtr MediaLibraryDenoise::dequeue_inference_callback_buffer()
{
    std::unique_lock<std::mutex> lock(m_inference_callback_mutex);
    m_inference_callback_condvar.wait(lock, [this] { return !m_inference_callback_queue.empty() || m_flushing; });
    if (m_inference_callback_queue.empty())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Inference callback queue is empty, returning nullptr");
        return nullptr;
    }
    NetworkInferenceBindingsPtr bindings = m_inference_callback_queue.front();
    m_inference_callback_queue.pop();
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("inference callback queue", m_inference_callback_queue.size(), DENOISE_TRACK,
                                      MEDIA_LIBRARY_CATEGORY);
    m_inference_callback_condvar.notify_one();
    return bindings;
}

// Timestamp queue controls

void MediaLibraryDenoise::queue_timestamp_buffer(timespec start_handle)
{
    std::unique_lock<std::mutex> lock(m_timestamp_mutex);
    m_timestamp_condvar.wait(lock, [this] { return m_timestamp_queue.size() < m_timestamp_queue_size; });
    m_timestamp_queue.push(start_handle);
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("timestamp queue", m_timestamp_queue.size(), DENOISE_TRACK,
                                      MEDIA_LIBRARY_CATEGORY);
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
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("timestamp queue", m_timestamp_queue.size(), DENOISE_TRACK,
                                      MEDIA_LIBRARY_CATEGORY);
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

void MediaLibraryDenoise::start_inference_callback_thread()
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Starting inference callback thread");
    if (!m_inference_callback_thread.joinable())
    {
        m_flushing = false;
        m_inference_callback_thread = std::thread(&MediaLibraryDenoise::inference_callback_thread, this);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Inference callback thread started successfully");
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Inference callback thread already running");
    }
}

void MediaLibraryDenoise::stop_inference_callback_thread()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Stopping inference callback thread");
    m_flushing = true;

    LOGGER__MODULE__TRACE(MODULE_NAME, "Notifying inference callback thread to stop");
    m_inference_callback_condvar.notify_all();

    m_hailort_denoise->wait_for_all_jobs_to_finish();
    join_inference_callback_thread(m_inference_callback_thread);

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Clearing callback queues");
    m_loopback_condvar.notify_all();
    clear_loopback_queue();
    m_timestamp_condvar.notify_all();
    clear_timestamp_queue();
}

bool MediaLibraryDenoise::is_packed_output() const
{
    return m_hailort_denoise->is_packed_output();
}

int MediaLibraryDenoise::get_denoised_output_index() const
{
    return m_hailort_denoise->get_denoised_output_index();
}

NetworkInferenceBindingsPtr MediaLibraryDenoise::create_bindings(HailoMediaLibraryBufferPtr input_buffer,
                                                                 HailoMediaLibraryBufferPtr output_buffer) const
{
    return m_hailort_denoise->create_bindings(input_buffer->get_attached_profile()->iq_settings.denoise, input_buffer,
                                              output_buffer);
}

media_library_return MediaLibraryDenoise::bind_loopback_buffers(NetworkInferenceBindingsPtr bindings,
                                                                const TensorBindings &loopback_buffers) const
{
    return m_hailort_denoise->bind_loopback_buffers(std::move(bindings), loopback_buffers);
}

media_library_return MediaLibraryDenoise::initialize_loopback_buffers(const TensorBindings &loopback_buffers)
{
    if (m_should_queue_dummy_loopback_buffer)
    {
        m_should_queue_dummy_loopback_buffer = false;
        for (int i = 0; i < m_loopback_limit; i++)
        {
            queue_loopback_buffer(loopback_buffers);
        }

        LOGGER__MODULE__INFO(MODULE_NAME, "Denoise loopback buffers initialized successfully");
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
