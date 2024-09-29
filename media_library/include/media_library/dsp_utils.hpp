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
#include "media_library_buffer.hpp"
#include <stdint.h>
#include <vector>
#include <optional>

#define MIN_ISP_AE_FPS_FOR_DIS (20)

/** @defgroup dsp_utils_definitions MediaLibrary DSP utilities CPP API
 * definitions
 *  @{
 */

/**
  DSP Interfaces and Utilities
*/

using DspImagePropertiesPtr = std::shared_ptr<dsp_image_properties_t>;

struct hailo_dsp_buffer_data_t
{
    dsp_data_plane_t planes[4] = {};
    dsp_image_properties_t properties = {};
    hailo_dsp_buffer_data_t(size_t width, size_t height, size_t planes_count, HailoFormat format, HailoMemoryType memory, std::vector<hailo_data_plane_t> hailo_data_planes)
    {
        properties.width = width;
        properties.height = height;

        switch (format)
        {
        case HAILO_FORMAT_NV12:
            properties.format = DSP_IMAGE_FORMAT_NV12;
            break;
        case HAILO_FORMAT_A420:
            properties.format = DSP_IMAGE_FORMAT_A420;
            break;
        case HAILO_FORMAT_ARGB:
            properties.format = DSP_IMAGE_FORMAT_ARGB;
            break;
        case HAILO_FORMAT_GRAY16:
            properties.format = DSP_IMAGE_FORMAT_GRAY16;
            break;
        default:
            //TODO: error 
            break;
        }

        switch (memory)
        {
        case HAILO_MEMORY_TYPE_CMA:
            properties.memory = DSP_MEMORY_TYPE_USERPTR;
            break;
        case HAILO_MEMORY_TYPE_DMABUF:
            properties.memory = DSP_MEMORY_TYPE_DMABUF;
            break;
        default:
            //TODO: error
            break;
        }
        properties.planes_count = planes_count;

        for (uint32_t i = 0; i < planes_count; i++)
        {
            planes[i].fd = hailo_data_planes[i].fd;
            planes[i].bytesperline = hailo_data_planes[i].bytesperline;
            planes[i].bytesused = hailo_data_planes[i].bytesused;
        }
        properties.planes = planes;
    }

    ~hailo_dsp_buffer_data_t() = default;

    // Move constructor
    hailo_dsp_buffer_data_t(hailo_dsp_buffer_data_t &&other) : properties(std::move(other.properties))
    {
        for (uint32_t i = 0; i < properties.planes_count; i++)
        {
            planes[i].fd = std::move(other.planes[i].fd);
            planes[i].bytesperline = std::move(other.planes[i].bytesperline);
            planes[i].bytesused = std::move(other.planes[i].bytesused);

            other.planes[i].fd = 0;
            other.planes[i].bytesperline = 0;
            other.planes[i].bytesused = 0;
        }

        properties.planes = planes;
    }
    // Move assignment delete
    hailo_dsp_buffer_data_t &operator=(hailo_dsp_buffer_data_t &&other) = delete;
    // Copy constructor delete
    hailo_dsp_buffer_data_t(const hailo_dsp_buffer_data_t &other) = delete;
    // Copy assignment delete
    hailo_dsp_buffer_data_t &operator=(const hailo_dsp_buffer_data_t &other) = delete;

};

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
  perform_resize(hailo_buffer_data_t *input_buffer_data,
                 hailo_buffer_data_t *output_buffer_data,
                 dsp_interpolation_type_t dsp_interpolation_type,
                 std::optional<dsp_letterbox_properties_t> letterbox_properties);

  dsp_status
  perform_resize(dsp_image_properties_t *input_image_properties,
                 dsp_image_properties_t *output_image_properties,
                 dsp_interpolation_type_t dsp_interpolation_type,
                 std::optional<dsp_letterbox_properties_t> letterbox_properties);

  dsp_status
  perform_crop_and_resize(hailo_buffer_data_t *input_buffer_data,
                          hailo_buffer_data_t *output_buffer_data,
                          crop_resize_dims_t args,
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
  perform_dsp_dewarp(hailo_buffer_data_t *input_buffer_data,
                      hailo_buffer_data_t *output_buffer_data,
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
                      dsp_interpolation_type_t interpolation,
                      const dsp_isp_vsm_t &isp_vsm,
                      const dsp_vsm_config_t &dsp_vsm_config,
                      const dsp_filter_angle_t &filter_angle,
                      uint16_t *cur_columns_sum,
                      uint16_t *cur_rows_sum,
                      bool do_mesh_correction);

  dsp_status 
  perform_dsp_dewarp(hailo_buffer_data_t *input_buffer_data,
                      hailo_buffer_data_t *output_buffer_data,
                      dsp_dewarp_mesh_t *mesh,
                      dsp_interpolation_type_t interpolation);

  dsp_status 
  perform_dsp_dewarp(dsp_image_properties_t *input_image_properties,
                     dsp_image_properties_t *output_image_properties,
                     dsp_dewarp_mesh_t *mesh,
                     dsp_interpolation_type_t interpolation);

  dsp_status perform_dsp_multiblend(hailo_buffer_data_t *input_buffer_data,
                                    dsp_overlay_properties_t *overlay,
                                    size_t overlays_count);

  dsp_status perform_dsp_multiblend(dsp_image_properties_t *image_frame,
                                    dsp_overlay_properties_t *overlay,
                                    size_t overlays_count);

  dsp_status hailo_buffer_data_to_dsp_buffer_data(hailo_buffer_data_t *buffer_data, hailo_dsp_buffer_data_t *out_dsp_buffer_data);

  dsp_status hailo_buffer_data_to_dsp_image_props(hailo_buffer_data_t *buffer_data, dsp_image_properties_t *out_dsp_buffer_props);

  void free_overlay_property_planes(dsp_overlay_properties_t *overlay_properties);
  void free_image_property_planes(dsp_image_properties_t *image_properties);

  size_t get_dsp_desired_stride_from_width(size_t width);

  static constexpr int max_blend_overlays = 50;

} // namespace dsp_utils

/** @} */ // end of dsp_utils_definitions
