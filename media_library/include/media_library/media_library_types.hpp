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
 * @file media_library_types.hpp
 * @brief MediaLibrary type definitions
 **/

#pragma once
#include "dis_common.h"
#include "dsp_utils.hpp"
#include <string>

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
  MEDIA_LIBRARY_BUFFER_NOT_FOUND,

    /** Max enum value to maintain ABI Integrity */
    MEDIA_LIBRARY_MAX = INT_MAX
};

struct mat_dims
{
    int width;
    int height;
    int baseline;
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
    DIGITAL_ZOOM_MODE_ROI = 0,
    DIGITAL_ZOOM_MODE_MAGNIFICATION,

    /** Max enum value to maintain ABI Integrity */
    DIGITAL_ZOOM_MODE_MAX = INT_MAX
};

enum rotation_angle_t
{
    ROTATION_ANGLE_0 = 0,
    ROTATION_ANGLE_90 = 1,
    ROTATION_ANGLE_180 = 2,
    ROTATION_ANGLE_270 = 3,

    /** Max enum value to maintain ABI Integrity */
    ROTATION_ANGLE_MAX = INT_MAX
};

enum denoise_method_t
{
    DENOISE_METHOD_NONE = 0,
    DENOISE_METHOD_VD1, // High quality
    DENOISE_METHOD_VD2, // Balanced
    DENOISE_METHOD_VD3, // High performance

    /** Max enum value to maintain ABI Integrity */
    DENOISE_METHOD_MAX = INT_MAX
};

enum class EncoderType
{
    None,
    Hailo,
    Jpeg
};

struct roi_t
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct vsm_config_t
{
    uint32_t vsm_h_size;
    uint32_t vsm_h_offset;
    uint32_t vsm_v_size;
    uint32_t vsm_v_offset;
};

struct hailort_t
{
    std::string device_id;
};

struct network_config_t
{
    std::string network_path;
    std::string y_channel;
    std::string uv_channel;
    std::string output_y_channel;
    std::string output_uv_channel;
};

struct feedback_network_config_t
{
    std::string network_path;
    std::string y_channel;
    std::string uv_channel;
    std::string feedback_y_channel;
    std::string feedback_uv_channel;
    std::string output_y_channel;
    std::string output_uv_channel;
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
        return sensor_calib_path == other.sensor_calib_path &&
               interpolation_type == other.interpolation_type;
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

struct optical_zoom_config_t
{
    bool enabled;
    float magnification;
    float max_dewarping_magnification;
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
        return angle == other.angle && enabled == other.enabled;
    }

    bool operator!=(const rotation_config_t &other) const
    {
        return !(*this == other);
    }

    rotation_angle_t effective_value() const
    {
        return enabled ? angle : ROTATION_ANGLE_0;
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
    bool dimensions_equal(const output_resolution_t &other, bool rotated = false) const
    {
        if (rotated)
            return dimensions.destination_width == other.dimensions.destination_height && dimensions.destination_height == other.dimensions.destination_width;
        return dimensions.destination_width == other.dimensions.destination_width && dimensions.destination_height == other.dimensions.destination_height;
    }
};

struct output_video_config_t
{
    dsp_interpolation_type_t interpolation_type;
    dsp_image_format_t format;
    bool grayscale;
    std::vector<output_resolution_t> resolutions;
};

struct input_video_config_t
{
    dsp_image_format_t format;
    output_resolution_t resolution;
    std::string video_device;

    bool operator==(const input_video_config_t &other) const
    {
        return format == other.format && resolution == other.resolution &&
               video_device == other.video_device;
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
    optical_zoom_config_t optical_zoom_config;
    digital_zoom_config_t digital_zoom_config;
    input_video_config_t input_video_config;

    pre_proc_op_configurations()
    {
        output_video_config.resolutions = std::vector<output_resolution_t>();
    }

    media_library_return update(pre_proc_op_configurations &pre_proc_op_configs)
    {
        rotation_config = pre_proc_op_configs.rotation_config;
        flip_config = pre_proc_op_configs.flip_config;
        dis_config = pre_proc_op_configs.dis_config;
        digital_zoom_config = pre_proc_op_configs.digital_zoom_config;
        dewarp_config.enabled = pre_proc_op_configs.dewarp_config.enabled;
        output_video_config.grayscale = pre_proc_op_configs.output_video_config.grayscale;
        output_video_config.interpolation_type = pre_proc_op_configs.output_video_config.interpolation_type;

        // TODO: can we change interpolation type?
        if (dewarp_config != pre_proc_op_configs.dewarp_config)
        {
            // Update dewarp configuration is restricted
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        if (input_video_config != pre_proc_op_configs.input_video_config)
        {
            // Update input video configuration which is restricted
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        for (uint8_t i = 0; i < pre_proc_op_configs.output_video_config.resolutions.size(); i++)
        {
            output_resolution_t &current_res = output_video_config.resolutions[i];
            output_resolution_t &new_res = pre_proc_op_configs.output_video_config.resolutions[i];
            if (!current_res.dimensions_equal(new_res))
            {
                // Update output video dimensions is restricted
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            current_res.framerate = new_res.framerate;
        }
        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct multi_resize_config_t
{
public:
    output_resolution_t input_video_config;
    output_video_config_t output_video_config;
    digital_zoom_config_t digital_zoom_config;
    rotation_angle_t rotation_config;

    multi_resize_config_t()
    {
        // Since we are not parsing the input_video_config, we need to set the default values
        input_video_config.framerate = 0;
        input_video_config.pool_max_buffers = 0;
        input_video_config.dimensions.destination_width = 0;
        input_video_config.dimensions.destination_height = 0;
        rotation_config = ROTATION_ANGLE_0;
        output_video_config.resolutions = std::vector<output_resolution_t>();
    }

    media_library_return update(multi_resize_config_t &mresize_config)
    {
        digital_zoom_config = mresize_config.digital_zoom_config;
        output_video_config.grayscale = mresize_config.output_video_config.grayscale;
        output_video_config.interpolation_type = mresize_config.output_video_config.interpolation_type;

        for (uint8_t i = 0; i < mresize_config.output_video_config.resolutions.size(); i++)
        {
            output_resolution_t &current_res = output_video_config.resolutions[i];
            output_resolution_t &new_res = mresize_config.output_video_config.resolutions[i];
            if (!current_res.dimensions_equal(new_res, rotation_config % 2))
            {
                // Update output video dimensions is restricted
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            current_res.framerate = new_res.framerate;
        }

        // rotate if necessary
        return set_output_dimensions_rotation(rotation_config);
    }

    media_library_return set_output_dimensions_rotation(const rotation_angle_t &rotation_angle)
    {
        if (rotation_angle % 2 == rotation_config % 2) // dimensions are the same
        {
            rotation_config = rotation_angle;
            return MEDIA_LIBRARY_SUCCESS;
        }

        // need to rotate frame dimensions
        for (output_resolution_t &current_res : output_video_config.resolutions)
        {
            std::swap(current_res.dimensions.destination_width, current_res.dimensions.destination_height);
        }
        rotation_config = rotation_angle;
        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct ldc_config_t
{
public:
    rotation_config_t rotation_config;
    flip_config_t flip_config;
    dewarp_config_t dewarp_config;
    dis_config_t dis_config;
    optical_zoom_config_t optical_zoom_config;
    input_video_config_t input_video_config;
    output_resolution_t output_video_config;

    ldc_config_t()
    {
        // Since we are not parsing input_video_config and output_video_config from json, we need to set the default values
        input_video_config.format = DSP_IMAGE_FORMAT_NV12;
        input_video_config.video_device = "";
        input_video_config.resolution.framerate = 0;
        input_video_config.resolution.pool_max_buffers = 10;
        input_video_config.resolution.dimensions.destination_width = 0;
        input_video_config.resolution.dimensions.destination_height = 0;

        output_video_config.framerate = 0;
        output_video_config.pool_max_buffers = 10;
        output_video_config.dimensions.destination_width = 0;
        output_video_config.dimensions.destination_height = 0;
    }

    media_library_return update(ldc_config_t &ldc_configs)
    {
        bool disable_dewarp = ldc_configs.optical_zoom_config.enabled &&
                              ldc_configs.optical_zoom_config.magnification >= ldc_configs.optical_zoom_config.max_dewarping_magnification;

        dewarp_config.enabled = disable_dewarp ? false : ldc_configs.dewarp_config.enabled;
        flip_config = ldc_configs.flip_config;
        dis_config = ldc_configs.dis_config;

        // TODO: can we change interpolation type?
        if (dewarp_config != ldc_configs.dewarp_config)
        {
            // Update dewarp configuration is restricted
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        rotation_angle_t current_rotation_angle = rotation_config.effective_value();
        rotation_angle_t new_rotation_angle = ldc_configs.rotation_config.effective_value();
        if (current_rotation_angle != new_rotation_angle)
        {
            if (current_rotation_angle % 2 != new_rotation_angle % 2) // if the rotation angle is not aligned, rotate the output resolutions
            {
                rotate_output_dimensions();
            }
        }
        rotation_config = ldc_configs.rotation_config;
        return MEDIA_LIBRARY_SUCCESS;
    }

private:
    void rotate_output_dimensions()
    {
        std::swap(output_video_config.dimensions.destination_width, output_video_config.dimensions.destination_height);
    }
};

struct denoise_config_t
{
public:
    bool enabled;
    std::string sensor;
    denoise_method_t denoising_quality;
    uint32_t loopback_count;
    feedback_network_config_t network_config;

    denoise_config_t()
    {
        enabled = false;
        sensor = "imx678";
        denoising_quality = DENOISE_METHOD_VD2;
        loopback_count = 1;
    }

    media_library_return update(denoise_config_t &denoise_configs)
    {
        enabled = denoise_configs.enabled;
        sensor = denoise_configs.sensor;
        denoising_quality = denoise_configs.denoising_quality;
        loopback_count = denoise_configs.loopback_count;
        network_config = denoise_configs.network_config;

        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct defog_config_t
{
public:
    bool enabled;
    network_config_t network_config;

    media_library_return update(defog_config_t &defog_configs)
    {
        enabled = defog_configs.enabled;
        network_config = defog_configs.network_config;

        return MEDIA_LIBRARY_SUCCESS;
    }
};

/** @} */ // end of media_library_types_definitions
