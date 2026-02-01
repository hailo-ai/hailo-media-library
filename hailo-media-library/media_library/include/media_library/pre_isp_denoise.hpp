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
#include "isp_manager.hpp"
#include "hailort_denoise.hpp"

#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <ctime>

class MediaLibraryPreIspDenoise : public MediaLibraryDenoise
{
  public:
    MediaLibraryPreIspDenoise(IspManager &isp_manager);
    ~MediaLibraryPreIspDenoise();

    // Ensure the correct HailoRT instance type (VD or HDM) based on configuration
    void ensure_correct_hailort_instance(const denoise_config_t &denoise_configs);

  protected:
    IspManager &m_isp_manager;

    // virtual functions to override
    void prepare_hailort_instance(const denoise_config_t &denoise_configs) override;
    void copy_meta(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer) override;

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

    media_library_return create_and_initialize_buffer_pools(const input_video_config_t &input_video_configs) override;
    media_library_return free_buffer_pools() override;
    media_library_return acquire_output_buffer(NetworkInferenceBindingsPtr bindings) override;

  private:
    bool process_inference(NetworkInferenceBindingsPtr bindings) override;
    static HailortAsyncDenoiseType determine_hdm_mode(const denoise_config_t &denoise_configs);

  public:
    bool is_enabled(const denoise_config_t &denoise_configs) override;
    using MediaLibraryDenoise::is_packed_output;
    static bool is_packed_output(const denoise_config_t &denoise_configs);
};
using MediaLibraryPreIspDenoisePtr = std::shared_ptr<MediaLibraryPreIspDenoise>;
