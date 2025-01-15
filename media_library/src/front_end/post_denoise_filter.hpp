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
#include <utility>

/**
 * @brief Denoise parameters recived from the ISP
 *
 */
struct __attribute__((packed)) post_denoise_config_t
{
    bool enabled;
    bool auto_luma;
    float manual_contrast;
    int32_t manual_brightness;
    float auto_percentile_low;
    float auto_percentile_high;
    uint8_t auto_target_low;
    uint8_t auto_target_high;
    float auto_low_pass_filter_alpha;
    uint32_t sharpness;
    float saturation;
};

class PostDenoiseFilter
{
  public:
    using Histogram = uint32_t[256];
    static std::pair<uint16_t, uint16_t> histogram_sample_step_for_frame(std::pair<size_t, size_t> frame_size);

    PostDenoiseFilter();
    ~PostDenoiseFilter();
    dsp_image_enhancement_params_t get_dsp_denoise_params();
    void set_dsp_denoise_params_from_histogram(const Histogram &histogram);
    bool is_enabled();
    bool m_denoise_element_enabled;

  private:
    static constexpr uint32_t histogram_sample_size = 10'000;

    void read_denoise_params_from_isp();
    void set_dsp_denoise_params_from_isp(const post_denoise_config_t &m_post_denoise_config);
    std::pair<uint8_t, uint8_t> find_percentile_pixels(const Histogram &histogram);
    std::pair<float, int16_t> contrast_brightness_from_percentiles(uint8_t low_percentile_pixel,
                                                                   uint8_t high_percentile_pixel);
    std::pair<float, float> contrast_brightness_lowpass_filter(float contrast, int16_t brightness);

    std::atomic<bool> m_enabled;
    std::atomic<bool> m_running;
    post_denoise_config_t m_post_denoise_config;
    dsp_image_enhancement_params_t m_denoise_params;
    dsp_image_enhancement_histogram_t m_histogram_params;
    // Denoise queue name from which the denoise parameters are read from the ISP[]
    static constexpr char post_denoise_isp_data[] = "/post_denoise_data";
    std::thread m_denoise_update_thread;
    std::shared_mutex m_post_denoise_lock;

    // m_denoise_params is int, and since the weight of the brightness calculated from the histogram might be small,
    // little changes will be casted away and the brightness value won't change over time so we use an additional
    // float value to track it
    std::optional<float> m_brightness;
};
