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

#include "denoise.hpp"
#include "buffer_pool.hpp"
#include "media_library_types.hpp"
#include "video_device.hpp"
#include "v4l2_ctrl.hpp"

#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <ctime>
#include <thread>
#include <queue>

class MediaLibraryPreIspDenoise : public MediaLibraryDenoise
{
  public:
    MediaLibraryPreIspDenoise(std::shared_ptr<v4l2::v4l2ControlManager> v4l2_ctrl_manager);
    ~MediaLibraryPreIspDenoise();

    // initialize the ISP
    media_library_return init();

    // deinitialize the ISP
    media_library_return deinit();

    // start the ISP thread
    media_library_return start();

    // stop the ISP thread
    media_library_return stop();

    // buffer wrapper
    void hailo_buffer_from_isp_buffer(HDR::VideoBuffer *video_buffer, HailoMediaLibraryBufferPtr buffer,
                                      std::function<void(HDR::VideoBuffer *buf)> on_free, HailoFormat format);

    // Ensure the correct HailoRT instance type (VD or HDM) based on configuration
    void ensure_correct_hailort_instance(const denoise_config_t &denoise_configs);

  protected:
    static constexpr int RAW_CAPTURE_BUFFERS_COUNT = 5;
    static constexpr int ISP_IN_BUFFERS_COUNT = 3;
    static constexpr const char *ISP_IN_PATH = "/dev/video10";
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";
    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 30;
    static constexpr size_t BITS_PER_PADDED_PIXEL = 16;
    static constexpr size_t BITS_PER_PACKED_PIXEL = 12;
    files_utils::SharedFd m_isp_fd;
    std::shared_ptr<HDR::VideoCaptureDevice> m_raw_capture_device;
    std::shared_ptr<HDR::VideoOutputDevice> m_isp_in_device;
    std::shared_ptr<HDR::DMABufferAllocator> m_allocator;
    std::shared_ptr<v4l2::v4l2ControlManager> m_v4l2_ctrl_manager;
    std::atomic<bool> m_initialized;
    bool m_is_hdm_mode;

    // ISP thread
    std::thread m_isp_thread;
    std::atomic<bool> m_isp_thread_running;

    media_library_return start_isp_thread();
    void stop_isp_thread();
    bool wait_for_stream_start();

    void write_output_buffer(HailoMediaLibraryBufferPtr output_buffer);

    // virtual functions to override
    bool currently_enabled() override;
    bool enabled(const denoise_config_t &denoise_configs) override;
    bool disabled(const denoise_config_t &denoise_configs) override;
    bool enable_changed(const denoise_config_t &denoise_configs) override;
    bool network_changed(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs) override;
    void prepare_hailort_instance(const denoise_config_t &denoise_configs) override;
    void copy_meta(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer) override;

    // dgain buffer pool
    static constexpr const char *BUFFER_POOL_NAME_DGAIN = "dgain_pool";
    std::shared_ptr<MediaLibraryBufferPool> m_dgain_buffer_pool;
    static constexpr int DGAIN_WIDTH = 1;
    static constexpr int DGAIN_HEIGHT = 1;
    static constexpr float DGAIN_FACTOR = 255.99225734;
    static constexpr uint16_t DGAIN_DIVISOR = 100;
    // bls buffer pool
    static constexpr const char *BUFFER_POOL_NAME_BLS = "bls_pool";
    std::shared_ptr<MediaLibraryBufferPool> m_bls_buffer_pool;
    static constexpr int BLS_WIDTH = 4;
    static constexpr int BLS_HEIGHT = 1;

    // HDM-specific buffer pools
    std::shared_ptr<MediaLibraryBufferPool> m_gamma_buffer_pool;
    static constexpr const char *GAMMA_BUFFER_POOL_NAME = "gamma_pool";
    std::queue<HailoMediaLibraryBufferPtr> m_gamma_buffer_queue;
    static constexpr size_t GAMMA_WIDTH = 960;
    static constexpr size_t GAMMA_HEIGHT = 540;
    static constexpr size_t GAMMA_FEATURES = 1;

    std::shared_ptr<MediaLibraryBufferPool> m_fusion_buffer_pool;
    static constexpr const char *FUSION_BUFFER_POOL_NAME = "fusion_pool";
    std::queue<HailoMediaLibraryBufferPtr> m_fusion_buffer_queue;
    static constexpr size_t FUSION_WIDTH = 960;
    static constexpr size_t FUSION_HEIGHT = 540;
    static constexpr size_t FUSION_FEATURES = 16;

    uint16_t get_dgain();
    uint16_t get_bls(v4l2::Video0Ctrl ctrl);

    media_library_return acquire_input_buffer(NetworkInferenceBindingsPtr bindings) override;
    media_library_return create_and_initialize_buffer_pools(const input_video_config_t &input_video_configs) override;
    media_library_return free_buffer_pools() override;
    media_library_return acquire_output_buffer(NetworkInferenceBindingsPtr bindings) override;

  private:
    bool process_inference(NetworkInferenceBindingsPtr bindings) override;
    bool determine_hdm_mode(const denoise_config_t &denoise_configs);
};
using MediaLibraryPreIspDenoisePtr = std::shared_ptr<MediaLibraryPreIspDenoise>;