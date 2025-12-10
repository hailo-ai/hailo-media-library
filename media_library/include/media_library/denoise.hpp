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
#pragma once

#include "encoder_config_types.hpp"
#include "hailort_denoise.hpp"
#include "config_manager.hpp"
#include "media_library_types.hpp"
#include "v4l2_ctrl.hpp"

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

class MediaLibraryDenoise
{
  public:
    class callbacks_t
    {
      public:
        std::function<void(bool)> on_enable_changed = nullptr;
        std::function<void(HailoMediaLibraryBufferPtr)> on_buffer_ready = nullptr;
        std::function<void(bool)> send_event = nullptr;
    };

    MediaLibraryDenoise();
    virtual ~MediaLibraryDenoise();
    MediaLibraryDenoise(MediaLibraryDenoise &&) = delete;
    MediaLibraryDenoise &operator=(MediaLibraryDenoise &&) = delete;

    // Configure the denoise module with new json string
    media_library_return configure(const std::string &config_string);

    // Configure the denoise module with denoise_config_t and hailort_t object
    media_library_return configure(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs,
                                   const input_video_config_t &input_video_configs);

    // Perform pre-processing on the input frame and return the output frames
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame);

    // get the denoise configurations object
    denoise_config_t get_denoise_configs();

    // get the hailort configurations object
    hailort_t get_hailort_configs();

    // get the enabled config status
    bool is_enabled();

    // set the callbacks object
    media_library_return observe(const MediaLibraryDenoise::callbacks_t &callbacks);

  protected:
    static constexpr size_t QUEUE_DEFAULT_SIZE = 4;

    static constexpr int HAILORT_SCHEDULER_THRESHOLD = 1;
    static constexpr std::chrono::milliseconds HAILORT_SCHEDULER_TIMEOUT{1000};
    static constexpr int HAILORT_SCHEDULER_BATCH_SIZE = 2;

    static constexpr size_t BUFFER_POOL_MAX_BUFFERS = 6;
    static constexpr int RESOULTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK = 16;

    // configured flag - to determine if first configuration was done
    // configuration manager
    ConfigManager m_denoise_config_manager;
    ConfigManager m_frontend_config_manager;
    ConfigManager m_hailort_config_manager;
    std::vector<MediaLibraryDenoise::callbacks_t> m_callbacks;
    // operation configurations
    denoise_config_t m_denoise_configs;
    hailort_t m_hailort_configs;
    input_video_config_t m_input_config;
    // configuration mutex
    std::shared_mutex rw_lock;
    // HRT module
    std::unique_ptr<HailortAsyncDenoise> m_hailort_denoise;
    // timestamp controls
    uint8_t m_timestamp_queue_size = QUEUE_DEFAULT_SIZE * 2;
    // loopback controls
    uint8_t m_queue_size = QUEUE_DEFAULT_SIZE;
    uint8_t m_loop_counter = 0;
    uint8_t m_loopback_batch_counter = 0;
    uint8_t m_loopback_limit = 1;
    bool m_configured = false;
    size_t m_sensor_index = 0;
    std::atomic<bool> m_flushing = false;
    // startup buffer
    HailoMediaLibraryBufferPtr m_startup_buffer = nullptr;

    // loopback queue controls
    std::condition_variable m_loopback_condvar;
    std::mutex m_loopback_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_loopback_queue;
    // timestamp queue controls
    std::condition_variable m_timestamp_condvar;
    std::mutex m_timestamp_mutex;
    std::queue<timespec> m_timestamp_queue;
    // callback queue controls
    std::condition_variable m_inference_callback_condvar;
    std::mutex m_inference_callback_mutex;
    std::queue<HailoMediaLibraryBufferPtr> m_inference_callback_queue;

    media_library_return decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs,
                                                   std::string config_string);
    media_library_return perform_denoise(HailoMediaLibraryBufferPtr input_buffer,
                                         HailoMediaLibraryBufferPtr output_buffer);
    media_library_return perform_initial_batch(HailoMediaLibraryBufferPtr input_buffer,
                                               HailoMediaLibraryBufferPtr output_buffer);
    media_library_return perform_subsequent_batches(HailoMediaLibraryBufferPtr input_buffer,
                                                    HailoMediaLibraryBufferPtr output_buffer);
    void stamp_time_and_log_fps(timespec &start_handle);
    void inference_callback(HailoMediaLibraryBufferPtr output_buffer);
    void queue_timestamp_buffer(timespec start_handle);
    std::optional<timespec> dequeue_timestamp_buffer();
    void clear_timestamp_queue();
    void queue_loopback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_loopback_buffer();
    void clear_loopback_queue();
    void inference_callback_thread();
    std::thread m_inference_callback_thread;
    void queue_inference_callback_buffer(HailoMediaLibraryBufferPtr buffer);
    HailoMediaLibraryBufferPtr dequeue_inference_callback_buffer();

    // Helper methods for derived classes to manage inference callback thread
    void start_inference_callback_thread();
    void stop_inference_callback_thread();

    // virtual functions to override
    virtual bool currently_enabled() = 0;
    virtual bool enabled(const denoise_config_t &denoise_configs) = 0;
    virtual bool disabled(const denoise_config_t &denoise_configs) = 0;
    virtual bool enable_changed(const denoise_config_t &denoise_configs) = 0;
    virtual bool network_changed(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs) = 0;
    virtual media_library_return create_and_initialize_buffer_pools(
        const input_video_config_t &input_video_configs) = 0;
    virtual media_library_return free_buffer_pools() = 0;
    virtual media_library_return acquire_output_buffer(HailoMediaLibraryBufferPtr output_buffer) = 0;
    virtual bool process_inference(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_buffer,
                                   HailoMediaLibraryBufferPtr output_buffer) = 0;
    virtual void copy_meta(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer) = 0;

    // Generate a startup buffer based on child class format
    virtual media_library_return generate_startup_buffer() = 0;
};
