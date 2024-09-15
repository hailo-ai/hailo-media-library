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
 * @file dewarp.hpp
 * @brief MediaLibrary Dewarp CPP API module
 **/

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <tl/expected.hpp>

#include "dsp_utils.hpp"
#include "buffer_pool.hpp"
#include "media_library_types.hpp"

/** @defgroup dewarp_type_definitions MediaLibrary Dewarp CPP API definitions
 *  @{
 */
class MediaLibraryDewarp;
using MediaLibraryDewarpPtr = std::shared_ptr<MediaLibraryDewarp>;

class MediaLibraryDewarp
{
protected:
    class Impl;
    std::shared_ptr<Impl> m_impl;

public:
    class callbacks_t
    {
    public:
        std::function<void(output_resolution_t &)> on_output_resolution_change = nullptr;
        std::function<void(rotation_angle_t &)> on_rotation_change = nullptr;
    };

    /**
     * @brief Create the dewarp module
     *
     * @param[in] config_string - json configuration string
     * @return tl::expected<MediaLibraryDewarpPtr, media_library_return> -
     * An expected object that holds either a shared pointer
     * to an MediaLibraryDewarp object, or a error code.
     */
    static tl::expected<std::shared_ptr<MediaLibraryDewarp>, media_library_return> create(std::string config_string);

    /**
     * @brief Constructor for the dewarp module
     *
     * @param[in] impl - shared pointer to the implementation object
     * @note This constructor is used internally by the create function.
     */
    MediaLibraryDewarp(std::shared_ptr<MediaLibraryDewarp::Impl> impl);

    /**
     * @brief Destructor for the dewarp module
     */
    ~MediaLibraryDewarp();

    media_library_return configure(std::string config_string);

    /**
     * @brief Configure the dewarp module with ldc_config_t object
     *
     * Update the ldc_config_t object
     * Initialize the dewarp mesh object for the DIS library
     * @param[in] ldc_config_t - ldc_config_t object
     * @return media_library_return - status of the configuration operation
     */
    media_library_return configure(ldc_config_t &ldc_configs);

    /**
     * @brief Perform dewarp on the input frame and return the output frames
     *
     * @param[in] input_frame - pointer to the input frame to be pre-processed
     * @param[out] output_frames - vector of output frames after dewarp
     *
     * @return media_library_return - status of the dewarp operation
     */
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame, HailoMediaLibraryBufferPtr output_frame);

    /**
     * @brief get the dewarp configurations object
     *
     * @return ldc_config_t - dewarp configurations
     */
    ldc_config_t &get_ldc_configs();

    /**
     * @brief get the input video configurations object
     *
     * @return input_video_config_t - input video configurations
     */
    input_video_config_t &get_input_video_config();

    /**
     * @brief get the output video configurations object
     *
     * @return output_resolution_t - output video configurations
     */
    output_resolution_t &get_output_video_config();

    /**
     * @brief set magnification level of optical zoom
     *
     * @param[in] magnification - magnification level of optical zoom
     *
     *  @return media_library_return - status of the operation
     */
    media_library_return set_optical_zoom(float magnification);

    /**
     * @brief set the input video configurations object
     *
     * @param[in] width - the new width of the input video
     * @param[in] height - the new height of the input video
     * @param[in] framerate - the new framerate of the input video
     * @param[in] format - the new format of the input video
     * @return media_library_return - status of the configuration operation
     */
    media_library_return set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate, HailoFormat format);

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
};

/** @} */ // end of dewarp_type_definitions
