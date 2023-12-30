/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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
 * @file vision_pre_proc.hpp
 * @brief MediaLibrary VisionPreProc CPP API module
 **/

#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <tl/expected.hpp>

#include "hailo_v4l2/hailo_vsm.h"
#include "dsp_utils.hpp"
#include "buffer_pool.hpp"
#include "media_library_types.hpp"

/** @defgroup multi_resize_type_definitions MediaLibrary Multiple Resize CPP API definitions
 *  @{
 */
class MediaLibraryMultiResize;
using MediaLibraryMultiResizePtr = std::shared_ptr<MediaLibraryMultiResize>;

class MediaLibraryMultiResize
{
protected:
  class Impl;
  std::shared_ptr<Impl> m_impl;

public:
  /**
   * @brief Create the multi-resize module
   *
   * @param[in] config_string - json configuration string
   * @return tl::expected<MediaLibraryMultiResizePtr, media_library_return> -
   * An expected object that holds either a shared pointer
   * to an MediaLibraryMultiResize object, or a error code.
   */
  static tl::expected<std::shared_ptr<MediaLibraryMultiResize>, media_library_return> create(std::string config_string);

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
   * @brief Configure the multi-resize module with new json string
   *
   * Read the json string and decode it to create the multi_resize_config_t object
   * @param[in] config_string - configuration json as string
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(std::string config_string);

  /**
   * @brief Configure the multi-resize module with multi_resize_config_t object
   *
   * Update the multi_resize_config_t object
   * @param[in] multi_resize_config_t - multi_resize_config_t object
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(multi_resize_config_t &mresize_config);

  /**
   * @brief Perform multi-resize on the input frame and return the output frames
   *
   * @param[in] input_frame - pointer to the input frame to be resized
   * @param[out] output_frames - vector of output frames after multi-resize
   *
   * @return media_library_return - status of the multi-resize operation
   */
  media_library_return handle_frame(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames);

  /**
   * @brief get the multi-resize configurations object
   *
   * @return multi_resize_config_t - multi-resize configurations
   */
  multi_resize_config_t &get_multi_resize_configs();

  /**
   * @brief get the output video configurations object
   *
   * @return output_video_config_t - output video configurations
   */
  output_video_config_t &get_output_video_config();

  /**
   * @brief set the input video configurations object
   *
   * @param[in] width - the new width of the input video
   * @param[in] height - the new height of the input video
   * @param[in] framerate - the new framerate of the input video
   * @return media_library_return - status of the configuration operation
   */
  media_library_return set_input_video_config(uint32_t width, uint32_t height, uint32_t framerate);
};

/** @} */ // end of multi_resize_type_definitions
