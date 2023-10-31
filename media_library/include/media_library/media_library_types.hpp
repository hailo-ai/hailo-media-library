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
 * @file media_library_types.hpp
 * @brief MediaLibrary type definitions
 **/

#pragma once
#include <string>

#include "dsp_utils.hpp"
#include "dis_common.h"

/** @defgroup media_library_types_definitions MediaLibrary Types CPP API definitions
 *  @{
 */

enum media_library_return
{
  MEDIA_LIBRARY_SUCCESS = 0,
  MEDIA_LIBRARY_ERROR,
  MEDIA_LIBRARY_INVALID_ARGUMENT,
  MEDIA_LIBRARY_CONFIGURATION_ERROR,
  MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR,
  MEDIA_LIBRARY_DSP_OPERATION_ERROR,
  MEDIA_LIBRARY_UNINITIALIZED,
  MEDIA_LIBRARY_OUT_OF_RESOURCES,
  MEDIA_LIBRARY_ENCODER_ENCODE_ERROR,
  MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS,

  /** Max enum value to maintain ABI Integrity */
  MEDIA_LIBRARY_MAX = INT_MAX
};

enum pre_proc_operation
{
  PRE_PROC_OPERATION_DEWARP = 0,
  PRE_PROC_OPERATION_DIS,
  PRE_PROC_OPERATION_DIGITAL_ZOOM,
  PRE_PROC_OPERATION_ROTATION,
  PRE_PROC_OPERATION_FLIP,
  PRE_PROC_OPERATION_GMV,
  
  /** Max enum value to maintain ABI Integrity */
  PRE_PROC_OPERATION_MAX = INT_MAX
};

enum flip_direction_t
{
  FLIP_DIRECTION_NONE = 0,
  FLIP_DIRECTION_HORIZONTAL,
  FLIP_DIRECTION_VERTICAL,
  FLIP_DIRECTION_BOTH,

  /** Max enum value to maintain ABI Integrity */
  FLIP_DIRECTION_MAX = INT_MAX
};

enum digital_zoom_mode_t
{
  DIGITAL_ZOOM_MODE_ROI  = 0,
  DIGITAL_ZOOM_MODE_MAGNIFICATION,

  /** Max enum value to maintain ABI Integrity */
  DIGITAL_ZOOM_MODE_MAX = INT_MAX
};

enum rotation_angle_t
{
  ROTATION_ANGLE_0 = 0,
  ROTATION_ANGLE_90,
  ROTATION_ANGLE_180,
  ROTATION_ANGLE_270,

  /** Max enum value to maintain ABI Integrity */
  ROTATION_ANGLE_MAX = INT_MAX
};

struct roi_t
{
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

struct dewarp_config_t
{
    bool enabled;
    std::string sensor_calib_path;
    dsp_interpolation_type_t interpolation_type;
    camera_type_t camera_type;

    /* 
    Diagonal FoV of output camera in degrees. The difference between input and output FOV, (horizontal, verticsal
    and diagonal) is the room for stabilization. Note the relation betwen aspect ratio and H,V,DFOV ratios:
     - for fisheye camera:
      HFOV / VFOV / DFOV = width / hight / diagonal
     - for pinhole camera:
      tan(HFOV/2) / tan(VFOV/2) / tan(DFOV/2) = width / hight / diagonal
     Set to <=0  to let DIS calculate and use the maximum possible FOV at the given input camera model and output
    aspect ratio.
    values: pinhole: 1-179, fisheye: or 1-360, degrees, no default, <=0 means "maximum possible FOV"
    */
    float camera_fov;

    bool operator==(const dewarp_config_t &other) const
    {
        return sensor_calib_path == other.sensor_calib_path && \
                interpolation_type == other.interpolation_type && \
                camera_type == other.camera_type && camera_fov == other.camera_fov;
    }
    bool operator!=(const dewarp_config_t &other) const
    {
        return !(*this == other);
    }
};

struct digital_zoom_config_t
{
  bool enabled;
  digital_zoom_mode_t mode;
  float magnification;
  roi_t roi;
};

struct flip_config_t
{
  bool enabled;
  flip_direction_t direction;
};

struct rotation_config_t
{
  bool enabled;
  rotation_angle_t angle;
  bool operator==(const rotation_config_t &other) const
  {
    return angle == other.angle;
  }
  bool operator!=(const rotation_config_t &other) const
  {
    return !(*this == other);
  }
};

struct output_resolution_t
{
  uint32_t framerate;
  uint32_t pool_max_buffers;
  dsp_utils::crop_resize_dims_t dimensions;
  bool operator==(const output_resolution_t &other) const
  {
    return framerate == other.framerate && dimensions.destination_width == other.dimensions.destination_width && dimensions.destination_height == other.dimensions.destination_height;
  }
  bool operator!=(const output_resolution_t &other) const
  {
    return !(*this == other);
  }
  bool operator==(const dsp_image_properties_t &dsp_image_props) const
  {
    return dimensions.destination_width == dsp_image_props.width && dimensions.destination_height == dsp_image_props.height;
  }
  bool operator!=(const dsp_image_properties_t &dsp_image_props) const
  {
    return !(*this == dsp_image_props);
  }
  bool dimensions_equal(const output_resolution_t &other) const
  {
    return dimensions.destination_width == other.dimensions.destination_width && dimensions.destination_height == other.dimensions.destination_height;
  }
};

struct output_video_config_t
{
  dsp_interpolation_type_t interpolation_type;
  dsp_image_format_t format;
  std::vector<output_resolution_t> resolutions;
};

struct input_video_config_t
{
  dsp_image_format_t format;
  output_resolution_t resolution;
  std::string video_device;
  bool operator==(const input_video_config_t &other) const
  {
    return format == other.format && resolution == other.resolution && video_device == other.video_device;
  }
  bool operator!=(const input_video_config_t &other) const
  {
    return !(*this == other);
  }
};

struct pre_proc_op_configurations
{
    public:
    output_video_config_t output_video_config;
    rotation_config_t rotation_config;
    flip_config_t flip_config;
    dewarp_config_t dewarp_config;
    dis_config_t dis_config;
    digital_zoom_config_t digital_zoom_config;
    input_video_config_t input_video_config;

    pre_proc_op_configurations() {
      output_video_config.resolutions = std::vector<output_resolution_t>();
    }

    media_library_return update(pre_proc_op_configurations &pre_proc_op_configs) {
      rotation_config = pre_proc_op_configs.rotation_config;
      flip_config = pre_proc_op_configs.flip_config;
      dis_config = pre_proc_op_configs.dis_config;
      digital_zoom_config = pre_proc_op_configs.digital_zoom_config;
      dewarp_config.enabled = pre_proc_op_configs.dewarp_config.enabled;
      // TODO: can we change interpolation type?
      if (dewarp_config != pre_proc_op_configs.dewarp_config)
      {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
      }
      if(input_video_config != pre_proc_op_configs.input_video_config)
      {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
      }

      for (uint8_t i=0; i<pre_proc_op_configs.output_video_config.resolutions.size(); i++)
      {
        output_resolution_t current_res = output_video_config.resolutions[i];
        output_resolution_t new_res = pre_proc_op_configs.output_video_config.resolutions[i];
        if (current_res.dimensions_equal(new_res))
        {
          return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        current_res.framerate = new_res.framerate;
      }
      return MEDIA_LIBRARY_SUCCESS;
    }
};

/** @} */ // end of media_library_types_definitions
