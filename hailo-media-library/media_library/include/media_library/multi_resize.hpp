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
 * @file multi_resize.hpp
 * @brief MediaLibrary Multi resize module
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
#include "privacy_mask.hpp"

/** @defgroup multi_resize_type_definitions MediaLibrary Multiple Resize CPP API definitions
 *  @{
 */
class MediaLibraryMultiResize;
using MediaLibraryMultiResizePtr = std::shared_ptr<MediaLibraryMultiResize>;

typedef std::unordered_map<std::string, HailoMediaLibraryBufferPtr> MultiResizeOutputBuffersMap;

class MediaLibraryMultiResize
{
  protected:
    class Impl;
    std::shared_ptr<Impl> m_impl;

  public:
    class callbacks_t
    {
      public:
        std::function<void(const config_application_input_streams_t &)> on_output_resolutions_change = nullptr;
    };

    /**
     * @brief Observes the media library by registering the provided callbacks.
     *
     * This function allows the user to observe the media library element by registering
     * callbacks that will be called when certain events occur.
     *
     * @param callbacks The callbacks to be registered for observation.
     * @return media_library_return - status of the observation operation
     */
    media_library_return observe(const callbacks_t &callbacks);

    /**
     * @brief Create the multi-resize module
     *
     * @return tl::expected<MediaLibraryMultiResizePtr, media_library_return> -
     * An expected object that holds either a shared pointer
     * to an MediaLibraryMultiResize object, or a error code.
     */
    static tl::expected<std::shared_ptr<MediaLibraryMultiResize>, media_library_return> create();

    /**
     * @brief Constructor for the multi-resize module
     *
     * @param[in] impl - shared pointer to the implementation object
     * @note This constructor is used internally by the create function.
     */
    MediaLibraryMultiResize(std::shared_ptr<MediaLibraryMultiResize::Impl> impl);

    /**
     * @brief Destructor for the multi-resize module
     */
    ~MediaLibraryMultiResize();

    /**
     * @brief Perform multi-resize on the input frame and return the output frames
     *
     * @param[in] input_frame - pointer to the input frame to be resized
     * @param[out] output_frames - vector of output frames after multi-resize
     *
     * @return media_library_return - status of the multi-resize operation
     */
    media_library_return handle_frame(HailoMediaLibraryBufferPtr input_frame,
                                      MultiResizeOutputBuffersMap &output_frames);

    /**
     * @brief Clean internal state on stop
     *
     * Clears previous rotation config and resolutions so that
     * the next start triggers a fresh output resolutions change callback.
     */
    void clean_on_stop();

    /* Test-only accessor */
    bool is_denoise_element_enabled() const;
};

/** @} */ // end of multi_resize_type_definitions
