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

#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <tl/expected.hpp>
#include <ctime>

class MediaLibraryPostIspDenoise final : public MediaLibraryDenoise
{
  public:
    MediaLibraryPostIspDenoise();
    ~MediaLibraryPostIspDenoise();

  private:
    // output buffer pool
    static constexpr const char *BUFFER_POOL_NAME = "post_isp_denoise_output";
    std::shared_ptr<MediaLibraryBufferPool> m_output_buffer_pool;

    // virtual functions to override
    bool currently_enabled() override;
    bool enabled(const denoise_config_t &denoise_configs) override;
    bool disabled(const denoise_config_t &denoise_configs) override;
    bool enable_changed(const denoise_config_t &denoise_configs) override;
    bool network_changed(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs) override;
    media_library_return create_and_initialize_buffer_pools(const input_video_config_t &input_video_configs) override;
    media_library_return free_buffer_pools() override;
    media_library_return acquire_output_buffer(NetworkInferenceBindingsPtr bindings) override;
    media_library_return acquire_input_buffer(NetworkInferenceBindingsPtr bindings) override;
    bool process_inference(NetworkInferenceBindingsPtr bindings) override;
    void copy_meta(HailoMediaLibraryBufferPtr input_buffer, HailoMediaLibraryBufferPtr output_buffer) override;
};
using MediaLibraryPostIspDenoisePtr = std::shared_ptr<MediaLibraryPostIspDenoise>;
