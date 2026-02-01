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
#include <optional>
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
        std::optional<std::function<void(output_resolution_t &)>> on_output_resolution_change;
        std::optional<std::function<void(bool)>> on_do_flip_rotate_change;
        std::optional<std::function<void(flip_direction_t)>> on_flip_change;
        std::optional<std::function<void(rotation_angle_t)>> on_rotation_change;
    };

    /**
     * @brief Create the dewarp module
     *
     * @param[in] config_string - json configuration string
     * @return tl::expected<MediaLibraryDewarpPtr, media_library_return> -
     * An expected object that holds either a shared pointer
     * to an MediaLibraryDewarp object, or a error code.
     */
    static tl::expected<std::shared_ptr<MediaLibraryDewarp>, media_library_return> create();

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
