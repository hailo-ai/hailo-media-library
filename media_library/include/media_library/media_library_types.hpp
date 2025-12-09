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
#include "encoder_config_types.hpp"
#include "imaging/aaa_config_types.hpp"
#include <cstdint>
#include <tl/expected.hpp>
#include <functional>
#include <string>
#include <ctime>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>

/** @defgroup media_library_types_definitions MediaLibrary Types CPP API definitions
 *  @{
 */

using output_stream_id_t = std::string;

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
    MEDIA_LIBRARY_FREETYPE_ERROR,
    MEDIA_LIBRARY_PROFILE_IS_RESTRICTED,

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

enum class frontend_src_element_t
{
    UNKNOWN = 0,
    V4L2SRC,
    APPSRC,

    /** Max enum value to maintain ABI Integrity */
    MAX = INT_MAX
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

enum class PrivacyMaskType
{
    COLOR,
    PIXELIZATION
};

enum motion_detection_sensitivity_levels_t
{
    LOWEST = 64,
    LOW = 32,
    MEDIUM = 16,
    HIGH = 8,
    HIGHEST = 4,

    /** Max enum value to maintain ABI Integrity */
    MOTION_DETECTION_SENSITIVITY_LEVELS_MAX = INT_MAX
};

enum sensor_index_t
{
    SENSOR_0 = 0,

    /** Max enum value to maintain ABI Integrity */
    SENSOR_INDEX_MAX = INT_MAX
};

enum class PipelineState
{
    VOID_PENDING,
    NULL_STATE,
    READY,
    PAUSED,
    PLAYING
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

struct isp_t
{
    std::string isp_config_files_path;
};

struct hailort_t
{
    std::string device_id;
};

enum hdr_dol_t
{
    HDR_DOL_2 = 2,
    HDR_DOL_3 = 3,
};

struct hdr_config_t
{
    bool enabled;
    float ls_ratio;
    float vs_ratio;
    hdr_dol_t dol;
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
    bool operator==(const feedback_network_config_t &other) const
    {
        return (network_path == other.network_path) && (y_channel == other.y_channel) &&
               (uv_channel == other.uv_channel) && (feedback_y_channel == other.feedback_y_channel) &&
               (feedback_uv_channel == other.feedback_uv_channel) && (output_y_channel == other.output_y_channel) &&
               (output_uv_channel == other.output_uv_channel);
    }
    std::string network_path;
    std::string y_channel;
    std::string uv_channel;
    std::string feedback_y_channel;
    std::string feedback_uv_channel;
    std::string output_y_channel;
    std::string output_uv_channel;
};

struct bayer_network_config_t
{
    bool operator==(const bayer_network_config_t &other) const
    {
        return (network_path == other.network_path) && (bayer_channel == other.bayer_channel) &&
               (feedback_bayer_channel == other.feedback_bayer_channel) && (dgain_channel == other.dgain_channel) &&
               (bls_channel == other.bls_channel) && (output_bayer_channel == other.output_bayer_channel);
    }
    std::string network_path;
    std::string bayer_channel;
    std::string feedback_bayer_channel;
    std::string dgain_channel;
    std::string bls_channel;
    std::string output_bayer_channel;
};

struct dewarp_config_t
{
    bool enabled;
    std::string sensor_calib_path;
    dsp_interpolation_type_t interpolation_type;
    camera_type_t camera_type;

    bool operator==(const dewarp_config_t &other) const
    {
        return sensor_calib_path == other.sensor_calib_path && interpolation_type == other.interpolation_type;
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
    float max_zoom_level;
};

struct flip_config_t
{
    bool enabled;
    flip_direction_t direction;

    bool operator==(const flip_config_t &other) const
    {
        return enabled == other.enabled && direction == other.direction;
    }

    bool operator!=(const flip_config_t &other) const
    {
        return !(*this == other);
    }

    flip_direction_t effective_value() const
    {
        return enabled ? direction : FLIP_DIRECTION_NONE;
    }
};

struct rotation_config_t
{
    bool enabled;
    rotation_angle_t angle;

    bool operator==(const rotation_config_t &other) const
    {
        return enabled == other.enabled && angle == other.angle;
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
    std::string stream_id;
    dsp_scaling_mode_t scaling_mode;

    bool operator==(const output_resolution_t &other) const
    {
        return framerate == other.framerate && dimensions.destination_width == other.dimensions.destination_width &&
               dimensions.destination_height == other.dimensions.destination_height &&
               scaling_mode == other.scaling_mode && stream_id == other.stream_id;
    }
    bool operator!=(const output_resolution_t &other) const
    {
        return !(*this == other);
    }
    bool operator==(const hailo_buffer_data_t &hailo_buffer_data) const
    {
        return dimensions.destination_width == hailo_buffer_data.width &&
               dimensions.destination_height == hailo_buffer_data.height;
    }
    bool operator!=(const hailo_buffer_data_t &hailo_buffer_data) const
    {
        return !(*this == hailo_buffer_data);
    }
    bool dimensions_equal(const output_resolution_t &other, bool rotated = false) const
    {
        if (rotated)
            return dimensions.destination_width == other.dimensions.destination_height &&
                   dimensions.destination_height == other.dimensions.destination_width;
        return dimensions.destination_width == other.dimensions.destination_width &&
               dimensions.destination_height == other.dimensions.destination_height;
    }

    bool dimensions_and_aspect_ratio_equal(const output_resolution_t &other, bool rotated = false) const
    {
        if (rotated)
            return dimensions.destination_width == other.dimensions.destination_height &&
                   dimensions.destination_height == other.dimensions.destination_width &&
                   scaling_mode == other.scaling_mode;
        return dimensions.destination_width == other.dimensions.destination_width &&
               dimensions.destination_height == other.dimensions.destination_height &&
               scaling_mode == other.scaling_mode;
    }
};

struct motion_detection_config_t
{
    bool enabled;
    output_resolution_t resolution;
    roi_t roi;
    motion_detection_sensitivity_levels_t sensitivity_level;
    float threshold;
    uint32_t buffer_pool_size;
};

struct config_application_input_streams_t
{
    dsp_interpolation_type_t interpolation_type;
    HailoFormat format;
    std::vector<output_resolution_t> resolutions;
};

struct application_input_streams_config_t
{
    dsp_interpolation_type_t interpolation_type;
    HailoFormat format;
    bool grayscale;
    std::vector<output_resolution_t> resolutions;

    application_input_streams_config_t(const config_application_input_streams_t &other, bool grayscale)
        : interpolation_type(other.interpolation_type), format(other.format), grayscale(grayscale),
          resolutions(other.resolutions)
    {
    }

    // Explicit copy constructor to avoid deprecation warning
    application_input_streams_config_t(const application_input_streams_config_t &other)
        : interpolation_type(other.interpolation_type), format(other.format), grayscale(other.grayscale),
          resolutions(other.resolutions)
    {
    }

    application_input_streams_config_t &operator=(const application_input_streams_config_t &other)
    {
        if (this != &other)
        {
            interpolation_type = other.interpolation_type;
            format = other.format;
            grayscale = other.grayscale;
            resolutions = other.resolutions;
        }
        return *this;
    }

    // Default constructor
    application_input_streams_config_t() = default;

    // Conversion operator to output_resolution_t (takes first resolution if available)
    operator output_resolution_t() const
    {
        if (!resolutions.empty())
        {
            return resolutions[0];
        }

        // Return default output_resolution_t if no resolutions available
        return output_resolution_t{.framerate = 30,
                                   .pool_max_buffers = 10,
                                   .dimensions = dsp_utils::crop_resize_dims_t{.perform_crop = 0,
                                                                               .crop_start_x = 0,
                                                                               .crop_end_x = 0,
                                                                               .crop_start_y = 0,
                                                                               .crop_end_y = 0,
                                                                               .destination_width = 1920,
                                                                               .destination_height = 1080},
                                   .stream_id = "",
                                   .scaling_mode = DSP_SCALING_MODE_STRETCH};
    }
};

struct input_video_config_t
{
    frontend_src_element_t source_type;
    HailoFormat format;
    output_resolution_t resolution;
    std::string source;
    size_t sensor_index; // Only sensor_index 0 is supported

    bool operator==(const input_video_config_t &other) const
    {
        return source_type == other.source_type && format == other.format && resolution == other.resolution &&
               source == other.source && sensor_index == other.sensor_index;
    }

    bool operator!=(const input_video_config_t &other) const
    {
        return !(*this == other);
    }
};

struct multi_resize_config_t
{
  public:
    output_resolution_t input_video_config;
    application_input_streams_config_t application_input_streams_config;
    digital_zoom_config_t digital_zoom_config;
    rotation_config_t rotation_config;
    motion_detection_config_t motion_detection_config;

    multi_resize_config_t()
    {
        // Since we are not parsing the input_video_config, we need to set the default values
        input_video_config.framerate = 0;
        input_video_config.pool_max_buffers = 0;
        input_video_config.dimensions.destination_width = 0;
        input_video_config.dimensions.destination_height = 0;
        rotation_config = {false, ROTATION_ANGLE_0};
        digital_zoom_config = digital_zoom_config_t();
        application_input_streams_config.resolutions = std::vector<output_resolution_t>();
        motion_detection_config = motion_detection_config_t();
    }

    media_library_return update(multi_resize_config_t &mresize_config)
    {
        digital_zoom_config = mresize_config.digital_zoom_config;
        motion_detection_config = mresize_config.motion_detection_config;
        application_input_streams_config.grayscale = mresize_config.application_input_streams_config.grayscale;
        application_input_streams_config.interpolation_type =
            mresize_config.application_input_streams_config.interpolation_type;

        for (uint8_t i = 0; i < mresize_config.application_input_streams_config.resolutions.size(); i++)
        {
            output_resolution_t &current_res = application_input_streams_config.resolutions[i];
            output_resolution_t &new_res = mresize_config.application_input_streams_config.resolutions[i];
            current_res.framerate = new_res.framerate;
            current_res.dimensions = new_res.dimensions;
        }

        // rotate if necessary
        return set_output_dimensions_rotation(mresize_config.rotation_config);
    }

    media_library_return set_output_dimensions_rotation(const rotation_config_t &new_rotation_config)
    {
        rotation_angle_t current_rotation_angle = rotation_config.effective_value();
        rotation_angle_t new_rotation_angle = new_rotation_config.effective_value();
        if (current_rotation_angle % 2 == new_rotation_angle % 2)
        {
            // new frame maybe rotated but has the same dimensions as current frame
            rotation_config = new_rotation_config;
            return MEDIA_LIBRARY_SUCCESS;
        }

        // need to rotate frame dimensions
        for (output_resolution_t &current_res : application_input_streams_config.resolutions)
        {
            if ((!is_portrait(current_res.dimensions) && is_portrait(new_rotation_angle)) ||
                (is_portrait(current_res.dimensions) && !is_portrait(new_rotation_angle)))
            {
                std::swap(current_res.dimensions.destination_width, current_res.dimensions.destination_height);
            }
        }
        rotation_config = new_rotation_config;
        if ((is_portrait(new_rotation_angle) &&
             !is_portrait(digital_zoom_config.roi.width, digital_zoom_config.roi.height)) ||
            (!is_portrait(new_rotation_angle) &&
             is_portrait(digital_zoom_config.roi.width, digital_zoom_config.roi.height)))
        {
            // rotate zoom
            std::swap(digital_zoom_config.roi.width, digital_zoom_config.roi.height);
            std::swap(digital_zoom_config.roi.x, digital_zoom_config.roi.y);
        }
        return MEDIA_LIBRARY_SUCCESS;
    }

    bool is_portrait(rotation_angle_t rotation_angle)
    {
        return (rotation_angle == ROTATION_ANGLE_90 || rotation_angle == ROTATION_ANGLE_270);
    }
    bool is_portrait(dsp_utils::crop_resize_dims_t &dimensions)
    {
        return dimensions.destination_width < dimensions.destination_height;
    }
    bool is_portrait(size_t width, size_t height)
    {
        return width <= height;
    }

    tl::expected<std::reference_wrapper<output_resolution_t>, media_library_return> get_output_resolution_by_index(
        uint8_t index)
    {
        if (index < application_input_streams_config.resolutions.size())
        {
            return std::ref(application_input_streams_config.resolutions[index]);
        }
        else if (motion_detection_config.enabled && index == application_input_streams_config.resolutions.size())
        {
            return std::ref(motion_detection_config.resolution);
        }
        else
        {
            return tl::unexpected(MEDIA_LIBRARY_ERROR);
        }
    }
};

struct eis_config_t
{
    bool enabled;
    bool stabilize;
    std::string eis_config_path;
    uint32_t window_size;
    double rotational_smoothing_coefficient;
    double iir_hpf_coefficient;
    float camera_fov_factor;
    uint64_t line_readout_time;
    uint8_t num_exposures;
    float hdr_exposure_ratio;
    float min_angle_deg;
    float max_angle_deg;
    uint32_t shakes_type_buff_size;
    uint32_t max_extensions_per_thr;
    uint32_t min_extensions_per_thr;
};

struct gyro_config_t
{
    bool enabled;
    std::string sensor_name;
    std::string sensor_frequency;
    double gyro_scale;
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
    output_resolution_t application_input_streams_config;
    eis_config_t eis_config;
    gyro_config_t gyro_config;

    ldc_config_t()
    {
        // Since we are not parsing input_video_config and application_input_streams_config from json, we need to set
        // the default values
        input_video_config.format = HAILO_FORMAT_NV12;
        input_video_config.source = "";
        input_video_config.sensor_index = 0;
        input_video_config.resolution.framerate = 0;
        input_video_config.resolution.pool_max_buffers = 10;
        input_video_config.resolution.dimensions.destination_width = 0;
        input_video_config.resolution.dimensions.destination_height = 0;
        input_video_config.resolution.dimensions.perform_crop = false;
        input_video_config.resolution.scaling_mode = DSP_SCALING_MODE_STRETCH;
        input_video_config.resolution.stream_id = "";

        application_input_streams_config.framerate = 0;
        application_input_streams_config.pool_max_buffers = 10;
        application_input_streams_config.dimensions.destination_width = 0;
        application_input_streams_config.dimensions.destination_height = 0;
    }

    media_library_return update(ldc_config_t &ldc_configs);

    void update_flip_rotate(ldc_config_t &ldc_configs);

    bool check_ops_enabled(bool dewarp_actions_only = false)
    {
        return (dewarp_config.enabled || dis_config.enabled || eis_config.enabled || gyro_config.enabled ||
                (!dewarp_actions_only && optical_zoom_config.enabled));
    }

    bool check_ops_enabled_changed(ldc_config_t &other)
    {
        return (dewarp_config.enabled != other.dewarp_config.enabled ||
                dis_config.enabled != other.dis_config.enabled || eis_config.enabled != other.eis_config.enabled ||
                gyro_config.enabled != other.gyro_config.enabled || flip_config.enabled != other.flip_config.enabled ||
                rotation_config.enabled != other.rotation_config.enabled ||
                optical_zoom_config.enabled != other.optical_zoom_config.enabled);
    }

  private:
    void rotate_output_dimensions()
    {
        std::swap(application_input_streams_config.dimensions.destination_width,
                  application_input_streams_config.dimensions.destination_height);
    }
};

struct denoise_config_t
{
    bool enabled;
    bool bayer;
    std::string sensor;
    denoise_method_t denoising_quality;
    uint32_t loopback_count;
    feedback_network_config_t network_config;
    bayer_network_config_t bayer_network_config;

    denoise_config_t()
    {
        enabled = false;
        bayer = false;
        sensor = "imx678";
        denoising_quality = DENOISE_METHOD_VD2;
        loopback_count = 1;
    }

    media_library_return update(denoise_config_t &denoise_configs)
    {
        enabled = denoise_configs.enabled;
        bayer = denoise_configs.bayer;
        sensor = denoise_configs.sensor;
        denoising_quality = denoise_configs.denoising_quality;
        loopback_count = denoise_configs.loopback_count;
        network_config = denoise_configs.network_config;
        bayer_network_config = denoise_configs.bayer_network_config;

        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct frontend_element_config_t
{
  public:
    ldc_config_t ldc_config;
    denoise_config_t denoise_config;
    multi_resize_config_t multi_resize_config;

    frontend_element_config_t() : ldc_config(), denoise_config(), multi_resize_config()
    {
    }

    frontend_element_config_t(ldc_config_t &ldc, denoise_config_t &denoise, multi_resize_config_t &multi_resize)
        : ldc_config(ldc), denoise_config(denoise), multi_resize_config(multi_resize)
    {
    }

    media_library_return update(frontend_element_config_t &frontend_element_configs)
    {
        ldc_config.update(frontend_element_configs.ldc_config);
        denoise_config.update(frontend_element_configs.denoise_config);
        multi_resize_config.update(frontend_element_configs.multi_resize_config);

        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct codec_config_t
{
    std::string stream_id;
    std::string config_path;
};

struct label_t
{
    std::string label;
    uint32_t id;
};

enum class AnalyticsType
{
    DETECTION,
    INSTANCE_SEGMENTATION,

    /** Max enum value to maintain ABI Integrity */
    ANALYTICS_TYPE_MAX = INT_MAX
};

enum class ScalingMode
{
    STRETCH,
    LETTERBOX_MIDDLE,
    LETTERBOX_UP_LEFT,

    /** Max enum value to maintain ABI Integrity */
    SCALING_MODE_MAX = INT_MAX
};

struct detection_analytics_config_t
{
    std::string analytics_data_id;
    ScalingMode scaling_mode;
    uint32_t width;
    uint32_t height;
    uint32_t original_width_ratio;
    uint32_t original_height_ratio;
    std::vector<label_t> labels;
    size_t max_entries;
};

struct instance_segmentation_analytics_config_t
{
    std::string analytics_data_id;
    ScalingMode scaling_mode;
    uint32_t width;
    uint32_t height;
    uint32_t original_width_ratio;
    uint32_t original_height_ratio;
    std::vector<label_t> labels;
    size_t max_entries;
};

struct application_analytics_config_t
{
    std::unordered_map<std::string, detection_analytics_config_t> detection_analytics_config;
    std::unordered_map<std::string, instance_segmentation_analytics_config_t> instance_segmentation_analytics_config;
};

struct frontend_config_t
{
  public:
    input_video_config_t input_config;
    ldc_config_t ldc_config;
    denoise_config_t denoise_config;
    multi_resize_config_t multi_resize_config;
    hdr_config_t hdr_config;
    hailort_t hailort_config;
    isp_t isp_config;
    application_analytics_config_t application_analytics_config;

    frontend_config_t()
        : input_config(), ldc_config(), denoise_config(), multi_resize_config(), hdr_config(), hailort_config(),
          isp_config(), application_analytics_config()
    {
    }

    media_library_return update(frontend_config_t &frontend_configs)
    {
        input_config = frontend_configs.input_config;
        ldc_config.update(frontend_configs.ldc_config);
        denoise_config.update(frontend_configs.denoise_config);
        multi_resize_config.update(frontend_configs.multi_resize_config);
        hdr_config = frontend_configs.hdr_config;
        hailort_config = frontend_configs.hailort_config;
        isp_config = frontend_configs.isp_config;

        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct encoder_bitrate_monitor
{
    bool enabled;
    uint32_t fps;
    uint32_t period;
    uint32_t sum_period;
    uint32_t ma_bitrate;
};

struct encoder_cycle_monitor
{
    bool enabled;
    uint32_t deviation_threshold;
    uint32_t monitor_frames;
    uint32_t start_delay;
    uint32_t frame_count;
    uint32_t sum;
    std::time_t start_time;
};

struct encoder_monitors
{
    encoder_bitrate_monitor bitrate_monitor;
    encoder_cycle_monitor cycle_monitor;
};

struct OverrideParameters
{
    std::string override_file;
    bool discard_on_profile_change;
};

struct profile_t
{
    std::string name;
    std::string config_file;
    nlohmann::json flattened_config_file_content;

    media_library_return flatten_n_validate_config();
};

struct medialib_config_t
{
    std::string default_profile;
    std::vector<profile_t> profiles;

    // get_profile(std::string name)
    tl::expected<profile_t, media_library_return> get_profile(const std::string &name) const
    {
        for (const auto &profile : profiles)
        {
            if (profile.name == name)
            {
                return profile;
            }
        }
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }
};

struct rgb_color_t
{
    uint r, g, b;
};

struct vertex
{
    int x, y;

    vertex() : x(0), y(0)
    {
    }
    vertex(int x, int y) : x(x), y(y)
    {
    }
};

struct polygon
{
    std::string id;
    std::vector<vertex> vertices;
};
using PolygonPtr = std::shared_ptr<polygon>;

struct static_privacy_mask_config_t
{
    bool enabled;
    std::vector<polygon> masks;
};

struct dynamic_privacy_mask_config_t
{
    bool enabled;
    std::string analytics_data_id;
    std::vector<std::string> masked_labels;
    size_t dilation_size;
};

struct privacy_mask_config_t
{
    PrivacyMaskType mask_type;
    uint32_t pixelization_size; // Range: 2 to 64
    rgb_color_t color_value;
    std::optional<dynamic_privacy_mask_config_t> dynamic_privacy_mask_config;
    std::optional<static_privacy_mask_config_t> static_privacy_mask_config;
};

struct calibration_header_t
{
    std::string creation_date;
    std::string creator;
    std::string sensor_name;
    std::string sample_name;
    std::string generator_version;
    std::vector<uint32_t> resolution;
};

struct config_input_video_t
{
    struct resolution_t
    {
        uint32_t width;
        uint32_t height;
        uint32_t framerate;
    } resolution;
    std::string source;
    frontend_src_element_t source_type;
    sensor_index_t sensor_index; // Only SENSOR_0 is supported
};

struct config_sensor_configuration_t
{
    std::string name;
    std::string drv;
    uint32_t mode;
    uint32_t pixel_mode;
    uint32_t sensor_only;
    int32_t af_i2c_bus;
    std::string af_i2c_addr;
    int32_t custom_readout_timing_short;

    bool operator==(const config_sensor_configuration_t &other) const
    {
        return name == other.name && drv == other.drv && mode == other.mode && pixel_mode == other.pixel_mode &&
               sensor_only == other.sensor_only && af_i2c_bus == other.af_i2c_bus && af_i2c_addr == other.af_i2c_addr &&
               custom_readout_timing_short == other.custom_readout_timing_short;
    }

    bool operator!=(const config_sensor_configuration_t &other) const
    {
        return !(*this == other);
    }
};

struct isp_format_config_sensor_configuration_t : public config_sensor_configuration_t
{
    bool hdr_enable;
    std::string sensor_calibration_file;
    uint32_t sensor_i2c_bus;
    std::string sensor_i2c_addr;

    isp_format_config_sensor_configuration_t(bool hdr_enable, std::string sensor_calibration_file,
                                             const config_sensor_configuration_t &sensor_configuration,
                                             uint32_t sensor_i2c_bus, std::string sensor_i2c_addr)
        : config_sensor_configuration_t(sensor_configuration), hdr_enable(hdr_enable),
          sensor_calibration_file(sensor_calibration_file), sensor_i2c_bus(sensor_i2c_bus),
          sensor_i2c_addr(sensor_i2c_addr)
    {
    }

    bool operator==(const isp_format_config_sensor_configuration_t &other) const
    {
        return config_sensor_configuration_t::operator==(other) && hdr_enable == other.hdr_enable &&
               sensor_calibration_file == other.sensor_calibration_file && sensor_i2c_bus == other.sensor_i2c_bus &&
               sensor_i2c_addr == other.sensor_i2c_addr;
    }

    bool operator!=(const isp_format_config_sensor_configuration_t &other) const
    {
        return !(*this == other);
    }
};

struct config_framerate_t
{
    std::string name;
    double fps;
};

struct config_resolution_entry_t
{
    std::string name;
    std::string id;
    double width;
    double height;
    std::vector<config_framerate_t> framerate;
};

struct config_calibration_header_t
{
    std::string creation_date;
    std::string creator;
    std::string sensor_name;
    std::string sample_name;
    std::string generator_version;
    std::vector<config_resolution_entry_t> resolution;
};

struct config_sensor_config_t
{
    std::string version;
    config_input_video_t input_video;
    config_sensor_configuration_t sensor_configuration;
    std::string sensor_calibration_file_path;
};

struct config_application_settings_t
{
    std::string version;
    config_application_input_streams_t application_input_streams;
    optical_zoom_config_t optical_zoom;
    digital_zoom_config_t digital_zoom;
    motion_detection_config_t motion_detection;
    rotation_config_t rotation;
    flip_config_t flip;
    hailort_t hailort;
    application_analytics_config_t application_analytics;
};

struct config_dis_angular_t
{
    bool enabled;
    nlohmann::json vsm; // Complex VSM configuration
};

struct config_dis_debug_t
{
    bool generate_resize_grid;
    bool fix_stabilization;
    double fix_stabilization_longitude;
    double fix_stabilization_latitude;
};

struct config_dis_t
{
    bool enabled;
    double minimun_coefficient_filter;
    double decrement_coefficient_threshold;
    double increment_coefficient_threshold;
    double running_average_coefficient;
    double std_multiplier;
    bool black_corners_correction_enabled;
    double black_corners_threshold;
    uint32_t average_luminance_threshold;
    double camera_fov_factor;
    config_dis_angular_t angular_dis;
    config_dis_debug_t debug;
};

struct config_eis_t
{
    bool enabled;
    bool stabilize;
    std::string eis_config_path;
    uint32_t window_size;
    double rotational_smoothing_coefficient;
    double iir_hpf_coefficient;
    double camera_fov_factor;
    uint64_t line_readout_time;
    double hdr_exposure_ratio;
};

struct config_gyro_t
{
    bool enabled;
    std::string sensor_name;
    std::string sensor_frequency;
    double scale;
};

struct config_stabilizer_settings_t
{
    std::string version;
    dis_config_t dis;
    eis_config_t eis;
    gyro_config_t gyro;
};

struct config_denoise_network_t
{
    std::string network_path;
    std::string y_channel;
    std::string uv_channel;
    std::string feedback_y_channel;
    std::string feedback_uv_channel;
    std::string output_y_channel;
    std::string output_uv_channel;
    std::string bayer_channel;
    std::string feedback_bayer_channel;
    std::string dgain_channel;
    std::string bls_channel;
    std::string output_bayer_channel;
};

struct config_denoise_t
{
    bool enabled;
    std::string sensor;
    std::string method;
    uint32_t loopback_count;
    config_denoise_network_t network;
    bool bayer;
};

struct config_hdr_t
{
    bool enabled;
    uint32_t dol;
    uint32_t lsRatio;
    uint32_t vsRatio;
};

struct config_gray_scale_t
{
    bool enabled;
};

struct config_iq_settings_t
{
    std::string version;
    config_gray_scale_t grayscale;
    denoise_config_t denoise;
    hdr_config_t hdr;
    dewarp_config_t dewarp;
    automatic_algorithms_config_t automatic_algorithms_config;
};

struct config_stream_osd_t
{
    nlohmann::json config; // Flexible OSD configuration
};

struct config_encoded_output_stream_t
{
    std::string stream_id;
    encoder_config_t encoding;
    config_stream_osd_t osd;
    privacy_mask_config_t masking;
};

struct config_profile_t
{
    std::string version;
    std::string name;
    config_sensor_config_t sensor_config;
    config_application_settings_t application_settings;
    config_stabilizer_settings_t stabilizer_settings;
    config_iq_settings_t iq_settings;
    std::vector<config_encoded_output_stream_t> encoded_output_streams;

    frontend_config_t to_frontend_config() const
    {
        frontend_config_t frontend_config;
        frontend_config.input_config = input_video_config_t{
            .source_type = sensor_config.input_video.source_type,
            .format = HAILO_FORMAT_NV12,
            .resolution =
                output_resolution_t{.framerate = sensor_config.input_video.resolution.framerate,
                                    .pool_max_buffers = 0,
                                    .dimensions =
                                        dsp_utils::crop_resize_dims_t{
                                            .perform_crop = 0,
                                            .crop_start_x = 0,
                                            .crop_end_x = 0,
                                            .crop_start_y = 0,
                                            .crop_end_y = 0,
                                            .destination_width = sensor_config.input_video.resolution.width,
                                            .destination_height = sensor_config.input_video.resolution.height},
                                    .stream_id = "",
                                    .scaling_mode = DSP_SCALING_MODE_STRETCH},
            .source = sensor_config.input_video.source,
            .sensor_index = 0};
        frontend_config.ldc_config.rotation_config = application_settings.rotation;
        frontend_config.ldc_config.flip_config = application_settings.flip;
        frontend_config.ldc_config.dewarp_config = iq_settings.dewarp;
        frontend_config.ldc_config.dis_config = stabilizer_settings.dis;
        frontend_config.ldc_config.optical_zoom_config = application_settings.optical_zoom;
        frontend_config.ldc_config.input_video_config = frontend_config.input_config;
        application_input_streams_config_t app_input_streams_config(application_settings.application_input_streams,
                                                                    iq_settings.grayscale.enabled);
        frontend_config.ldc_config.application_input_streams_config = app_input_streams_config;
        frontend_config.ldc_config.eis_config = stabilizer_settings.eis;
        frontend_config.ldc_config.gyro_config = stabilizer_settings.gyro;
        frontend_config.denoise_config = iq_settings.denoise;
        frontend_config.multi_resize_config.input_video_config = output_resolution_t{
            .framerate = sensor_config.input_video.resolution.framerate,
            .pool_max_buffers = 0,
            .dimensions =
                dsp_utils::crop_resize_dims_t{.perform_crop = 0,
                                              .crop_start_x = 0,
                                              .crop_end_x = 0,
                                              .crop_start_y = 0,
                                              .crop_end_y = 0,
                                              .destination_width = sensor_config.input_video.resolution.width,
                                              .destination_height = sensor_config.input_video.resolution.height},
            .stream_id = "",
            .scaling_mode = DSP_SCALING_MODE_STRETCH};
        frontend_config.multi_resize_config.application_input_streams_config = app_input_streams_config;
        frontend_config.multi_resize_config.digital_zoom_config = application_settings.digital_zoom;
        frontend_config.multi_resize_config.rotation_config = application_settings.rotation;
        frontend_config.multi_resize_config.motion_detection_config = application_settings.motion_detection;
        frontend_config.hdr_config = iq_settings.hdr;
        frontend_config.hailort_config = application_settings.hailort;
        frontend_config.isp_config = isp_t{
            .isp_config_files_path = "/usr/bin",
        };
        frontend_config.application_analytics_config = application_settings.application_analytics;
        return frontend_config;
    }

    // self to encoder_config_t
    std::map<output_stream_id_t, encoder_config_t> to_encoder_config_map() const
    {
        std::map<output_stream_id_t, encoder_config_t> encoder_configs;
        for (const auto &stream : encoded_output_streams)
        {
            encoder_configs[stream.stream_id] = stream.encoding;
        }
        return encoder_configs;
    }

    std::map<output_stream_id_t, config_encoded_output_stream_t> to_encoded_output_stream_config_map() const
    {
        std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_streams_map;
        for (const auto &stream : encoded_output_streams)
        {
            encoded_output_streams_map[stream.stream_id] = stream;
        }
        return encoded_output_streams_map;
    }

    // Get encoder type for specific stream (compatibility with ProfileConfig interface)
    EncoderType get_encoder_type(const output_stream_id_t &output_stream_id) const
    {
        auto encoder_configs = to_encoder_config_map();
        const auto &config = encoder_configs.at(output_stream_id);
        if (std::holds_alternative<jpeg_encoder_config_t>(config))
        {
            return EncoderType::Jpeg;
        }
        else if (std::holds_alternative<hailo_encoder_config_t>(config))
        {
            return EncoderType::Hailo;
        }
        else
        {
            return EncoderType::None;
        }
    }

    void from_frontend_config(const frontend_config_t &frontend_config)
    {
        // Convert sensor config
        sensor_config.input_video.resolution.width =
            frontend_config.input_config.resolution.dimensions.destination_width;
        sensor_config.input_video.resolution.height =
            frontend_config.input_config.resolution.dimensions.destination_height;
        sensor_config.input_video.resolution.framerate = frontend_config.input_config.resolution.framerate;

        application_settings.application_input_streams.format =
            frontend_config.multi_resize_config.application_input_streams_config.format;
        application_settings.application_input_streams.interpolation_type =
            frontend_config.multi_resize_config.application_input_streams_config.interpolation_type;
        application_settings.application_input_streams.resolutions =
            frontend_config.multi_resize_config.application_input_streams_config.resolutions;
        application_settings.optical_zoom = frontend_config.ldc_config.optical_zoom_config;
        application_settings.digital_zoom = frontend_config.multi_resize_config.digital_zoom_config;
        application_settings.motion_detection = frontend_config.multi_resize_config.motion_detection_config;
        application_settings.rotation = frontend_config.multi_resize_config.rotation_config;
        application_settings.flip = frontend_config.ldc_config.flip_config;
        application_settings.hailort = frontend_config.hailort_config;
        application_settings.application_analytics = frontend_config.application_analytics_config;

        // Convert stabilizer settings
        stabilizer_settings.dis = frontend_config.ldc_config.dis_config;
        stabilizer_settings.eis = frontend_config.ldc_config.eis_config;
        stabilizer_settings.gyro = frontend_config.ldc_config.gyro_config;

        // Convert IQ settings
        iq_settings.denoise = frontend_config.denoise_config;
        iq_settings.hdr = frontend_config.hdr_config;
        iq_settings.dewarp = frontend_config.ldc_config.dewarp_config;
    }
};

/** @} */ // end of media_library_types_definitions
