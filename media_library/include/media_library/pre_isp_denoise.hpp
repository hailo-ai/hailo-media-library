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

enum isp_mcm_mode
{
    ISP_MCM_MODE_OFF = 0,
    ISP_MCM_MODE_STITCHING = 1, // default mode for MCM
    ISP_MCM_MODE_INJECTION = 2, // read raw and write back to MCM, 16 bit
    ISP_MCM_MODE_PACKED = 3,    // read raw and write back to MCM, 12 bit
    ISP_MCM_MODE_MAX
};

class MediaLibraryPreIspDenoise final : public MediaLibraryDenoise
{
  public:
    MediaLibraryPreIspDenoise();
    ~MediaLibraryPreIspDenoise();

    // start the ISP thread
    media_library_return start();

    // stop the ISP thread
    media_library_return stop();

    // buffer wrapper
    void hailo_buffer_from_isp_buffer(HDR::VideoBuffer *video_buffer, HailoMediaLibraryBufferPtr buffer,
                                      std::function<void(HDR::VideoBuffer *buf)> on_free, HailoFormat format);

  private:
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

    static constexpr int RAW_CAPTURE_BUFFERS_COUNT = 5;
    static constexpr int ISP_IN_BUFFERS_COUNT = 3;
    static constexpr const char *YUV_STREAM_PATH = "/dev/video0";
    static constexpr const char *RAW_CAPTURE_PATH = "/dev/video2";
    static constexpr const char *ISP_IN_PATH = "/dev/video3";
    static constexpr const char *DMA_HEAP_PATH = "/dev/dma_heap/linux,cma";
    static constexpr int RAW_CAPTURE_DEFAULT_FPS = 30;
    static constexpr size_t BITS_PER_INPUT = 16;
    static constexpr size_t BITS_PER_OUTPUT = 16;
    int m_isp_fd;
    std::shared_ptr<HDR::VideoCaptureDevice> m_raw_capture_device;
    std::shared_ptr<HDR::VideoOutputDevice> m_isp_in_device;
    std::shared_ptr<HDR::DMABufferAllocator> m_allocator;

    // ISP thread
    std::thread m_isp_thread;
    std::atomic<bool> m_isp_thread_running;

    media_library_return start_isp_thread();
    media_library_return stop_isp_thread();
    void wait_for_stream_start();
    bool set_isp_mcm_mode(uint32_t target_mcm_mode);
    uint16_t get_dgain();
    uint16_t get_bls(v4l2::Video0Ctrl ctrl);
    std::pair<int, size_t> get_pixel_format_and_width(const size_t bits_per_pixel);
    void write_output_buffer(HailoMediaLibraryBufferPtr output_buffer);

    // virtual functions to override
    bool currently_enabled() override;
    bool enabled(const denoise_config_t &denoise_configs) override;
    bool disabled(const denoise_config_t &denoise_configs) override;
    bool enable_changed(const denoise_config_t &denoise_configs) override;
    bool network_changed(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs) override;
    media_library_return create_and_initialize_buffer_pools(const input_video_config_t &input_video_configs) override;
    media_library_return close_buffer_pools() override;
    media_library_return acquire_output_buffer(HailoMediaLibraryBufferPtr output_buffer) override;
    media_library_return acquire_dgain_buffer(HailoMediaLibraryBufferPtr dgain_buffer);
    media_library_return acquire_bls_buffer(HailoMediaLibraryBufferPtr bls_buffer);
    bool process_inference(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr loopback_buffer,
                           HailoMediaLibraryBufferPtr output_buffer) override;
    void copy_meta(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer) override;
    media_library_return generate_startup_buffer() override;
};
using MediaLibraryPreIspDenoisePtr = std::shared_ptr<MediaLibraryPreIspDenoise>;
