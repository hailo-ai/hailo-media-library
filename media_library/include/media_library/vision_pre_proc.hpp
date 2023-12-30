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

/** @defgroup vision_pre_proc_type_definitions MediaLibrary VisionPreProc CPP API definitions
 *  @{
 */
class MediaLibraryVisionPreProc;
using MediaLibraryVisionPreProcPtr = std::shared_ptr<MediaLibraryVisionPreProc>;

class MediaLibraryVisionPreProc
{
protected:
  class Impl;
  std::shared_ptr<Impl> m_impl;

public:
  /**
   * @brief Create the vision pre-processing module
   *
   * @param[in] config_string - json configuration string
   * @return tl::expected<MediaLibraryVisionPreProcPtr, media_library_return> -
   * An expected object that holds either a shared pointer
   * to an MediaLibraryVisionPreProc object, or a error code.
   */
  static tl::expected<std::shared_ptr<MediaLibraryVisionPreProc>, media_library_return> create(std::string config_string);

  /**
   * @brief Constructor for the vision pre-processing module
   *
   * @param[in] impl - shared pointer to the implementation object
   * @note This constructor is used internally by the create function.
   */
  MediaLibraryVisionPreProc(std::shared_ptr<MediaLibraryVisionPreProc::Impl> impl);

  /**
   * @brief Destructor for the vision pre-processing module
   */
  ~MediaLibraryVisionPreProc();

  /**
   * @brief Configure the pre-processing module with new json string
   *
   * Read the json string and decode it to create the pre_proc_op_configurations object
   * Initialize the dewarp mesh object for the DIS library
   * @param[in] config_string - configuration json as string
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(std::string config_string);

  /**
   * @brief Configure the pre-processing module with pre_proc_op_configurations object
   *
   * Update the pre_proc_op_configurations object
   * Initialize the dewarp mesh object for the DIS library
   * @param[in] pre_proc_op_configurations - pre_proc_op_configurations object
   * @return media_library_return - status of the configuration operation
   */
  media_library_return configure(pre_proc_op_configurations &pre_proc_op_configs);

  /**
   * @brief Perform pre-processing on the input frame and return the output frames
   *
   * @param[in] input_frame - pointer to the input frame to be pre-processed
   * @param[out] output_frames - vector of output frames after pre-processing
   *
   * @return media_library_return - status of the pre-processing operation
   */
  media_library_return handle_frame(hailo_media_library_buffer &input_frame, std::vector<hailo_media_library_buffer> &output_frames);

  /**
   * @brief get the pre-processing configurations object
   *
   * @return pre_proc_op_configurations - pre-processing configurations
   */
  pre_proc_op_configurations &get_pre_proc_configs();

  /**
   * @brief get the output video configurations object
   *
   * @return output_video_config_t - output video configurations
   */
  output_video_config_t &get_output_video_config();

  /**
   * @brief set magnification level of optical zoom
   *
   * @param[in] magnification - magnification level of optical zoom
   *
   *  @return media_library_return - status of the operation
   */
  media_library_return set_optical_zoom(float magnification);
};

/** @} */ // end of vision_pre_proc_type_definitions
