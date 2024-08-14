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
 * @file dsp_utils.hpp
 * @brief MediaLibrary DSP CPP API module
 **/

#pragma once

#include "hailo/hailodsp.h"
#include <stdint.h>
#include <vector>
#include <optional>

#define MIN_ISP_AE_FPS_FOR_DIS (20)

/** @defgroup dsp_utils_definitions MediaLibrary DSP utilities CPP API
 * definitions
 *  @{
 */

/**
  DSP interfaces and utilities
*/
namespace dsp_utils
{
  /**
    Crop and resize parameters
  */
  typedef struct
  {
    int perform_crop;
    size_t crop_start_x;
    size_t crop_end_x;
    size_t crop_start_y;
    size_t crop_end_y;
    size_t destination_width;
    size_t destination_height;
  } crop_resize_dims_t;

  dsp_status release_device();
  dsp_status acquire_device();
  dsp_status create_hailo_dsp_buffer(size_t size, void **buffer, bool dma = false);
  dsp_status release_hailo_dsp_buffer(void *buffer);

  dsp_status
  perform_resize(dsp_image_properties_t *input_image_properties,
                 dsp_image_properties_t *output_image_properties,
                 dsp_interpolation_type_t dsp_interpolation_type,
                 std::optional<dsp_letterbox_properties_t> letterbox_properties);

  dsp_status
  perform_crop_and_resize(dsp_image_properties_t *input_image_properties,
                          dsp_image_properties_t *output_image_properties,
                          crop_resize_dims_t args,
                          dsp_interpolation_type_t dsp_interpolation_type,
                          std::optional<dsp_letterbox_properties_t> letterbox_properties);

  dsp_status
  perform_dsp_multi_resize(dsp_multi_crop_resize_params_t *multi_crop_resize_params);

  dsp_status 
  perform_dsp_multi_resize(dsp_multi_crop_resize_params_t *multi_crop_resize_params, dsp_privacy_mask_t *privacy_mask_params);

  dsp_status 
  perform_dsp_dewarp(dsp_image_properties_t *input_image_properties,
                                dsp_image_properties_t *output_image_properties,
                                dsp_dewarp_mesh_t *mesh,
                                dsp_interpolation_type_t interpolation,
                                const dsp_isp_vsm_t &isp_vsm,
                                const dsp_vsm_config_t &dsp_vsm_config,
                                const dsp_filter_angle_t &filter_angle,
                                uint16_t *cur_columns_sum,
                                uint16_t *cur_rows_sum,
                                bool do_mesh_correction);

  dsp_status 
  perform_dsp_dewarp(dsp_image_properties_t *input_image_properties,
                                dsp_image_properties_t *output_image_properties,
                                dsp_dewarp_mesh_t *mesh,
                                dsp_interpolation_type_t interpolation);


  dsp_status perform_dsp_multiblend(dsp_image_properties_t *image_frame,
                                    dsp_overlay_properties_t *overlay,
                                    size_t overlays_count);
  void free_overlay_property_planes(dsp_overlay_properties_t *overlay_properties);
  void free_image_property_planes(dsp_image_properties_t *image_properties);

  size_t get_dsp_desired_stride_from_width(size_t width);

  static constexpr int max_blend_overlays = 50;

} // namespace dsp_utils

/** @} */ // end of dsp_utils_definitions
