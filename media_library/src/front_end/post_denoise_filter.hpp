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
/**
 * @file post_denoise_filter.hpp
 * @brief MediaLibrary  Post Denoise Filter module
 **/

#pragma once

#include "media_library_logger.hpp"
#include "dsp_utils.hpp"
#include <mqueue.h>
#include <functional>
#include <shared_mutex>
#include <thread>

/**
 * @brief Denoise parameters recived from the ISP
 *
 */
struct __attribute__((packed)) post_denoise_config_t
{
    bool enabled;
    uint32_t sharpness;
    float contrast;
    int32_t brightness;
    float saturation_u_a;
    float saturation_v_a;
    int32_t saturation_u_b;
    int32_t saturation_v_b;
};

class PostDenoiseFilter
{
  public:
    PostDenoiseFilter();
    ~PostDenoiseFilter();
    void get_denoise_params(dsp_image_enhancement_params_t &denoise_params);
    bool is_enabled();
    bool m_denoise_element_enabled;

  private:
    void denoise_read_denoise_params();

    std::atomic<bool> m_enabled;
    std::atomic<bool> m_running;
    post_denoise_config_t m_post_denoise_config;
    dsp_image_enhancement_params_t m_denoise_params;
    std::thread m_denoise_update_thread;
    std::shared_mutex m_post_denoise_lock;
    // Denoise queue name from which the denoise parameters are read from the ISP[]
    std::string post_denoise_isp_data = "/post_denoise_data";
};
