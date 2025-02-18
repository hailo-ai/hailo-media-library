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
 * @file denoise.hpp
 * @brief MediaLibrary Denoise CPP API module
 **/

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <tl/expected.hpp>

#include "buffer_pool.hpp"
#include "media_library_types.hpp"

/** @defgroup pre_isp_denoise_type_definitions MediaLibrary Pre ISP Denoise CPP API definitions
 *  @{
 */
class MediaLibraryPreIspDenoise;
using MediaLibraryPreIspDenoisePtr = std::shared_ptr<MediaLibraryPreIspDenoise>;

class MediaLibraryPreIspDenoise
{
  protected:
    class Impl;
    std::unique_ptr<Impl> m_impl;

  public:
    class callbacks_t
    {
      public:
        std::function<void(bool)> on_enable_changed = nullptr;
        std::function<void(HailoMediaLibraryBufferPtr)> on_buffer_ready = nullptr;
        std::function<void(bool)> send_event = nullptr;
    };

    /**
     * @brief Constructor for the denoise module
     */
    MediaLibraryPreIspDenoise();

    /**
     * @brief Destructor for the denoise module
     */
    ~MediaLibraryPreIspDenoise();

    /**
     * @brief Configure the denoise module with new json string
     *
     * Read the json string and decode it to create the denoise_config_t object
     * @param[in] config_string - configuration json as string
     * @return media_library_return - status of the configuration operation
     */
    media_library_return configure(const std::string &config_string);

    /**
     * @brief Configure the denoise module with denoise_config_t object
     *
     * Update the denoise_config_t object
     * @param[in] denoise_config_t - denoise_config_t object
     * @param[in] hailort_t - hailort_t object
     * @return media_library_return - status of the configuration operation
     */
    media_library_return configure(const denoise_config_t &denoise_configs, const hailort_t &hailort_configs);

    /**
     * @brief get the denoise configurations object
     *
     * @return denoise_config_t - denoise configurations
     */
    denoise_config_t get_denoise_configs();

    /**
     * @brief get the hailort configurations object
     *
     * @return hailort_t - hailort configurations
     */
    hailort_t get_hailort_configs();

    /**
     * @brief check enabled flag
     *
     * @return bool - enabled config flag
     */
    bool is_enabled();

    /**
     * @brief Observes the media library by registering the provided callbacks.
     *
     * This function allows the user to observe the media library by registering
     * callbacks that will be called when certain events occur.
     *
     * @param callbacks The callbacks to be registered for observation.
     * @return media_library_return - status of the observation operation
     */
    media_library_return observe(const callbacks_t &callbacks);

    /**
     * @brief Start the pre ISP denoise module, must be called before /dev/video0 is opened
     *
     * @return media_library_return - status of the operation
     */
    media_library_return start();

    /**
     * @brief Stop the pre ISP denoise module
     *
     * @return media_library_return - status of the operation
     */
    media_library_return stop();
};

/** @} */ // end of pre_isp_denoise_type_definitions
