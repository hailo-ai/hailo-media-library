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

#pragma once
#include "media_library_logger.hpp"
#include "media_library_types.hpp"
#include <iostream>
#include <nlohmann/json-schema.hpp>

#define MEDIALIB_JSON_SERIALIZE_ENUM(ENUM_TYPE, ...)                           \
    template <typename BasicJsonType>                                          \
    inline void to_json(BasicJsonType &j, const ENUM_TYPE &e)                  \
    {                                                                          \
        static_assert(std::is_enum<ENUM_TYPE>::value,                          \
                      #ENUM_TYPE " must be an enum!");                         \
        static const std::pair<ENUM_TYPE, BasicJsonType> m[] = __VA_ARGS__;    \
        auto it = std::find_if(                                                \
            std::begin(m), std::end(m),                                        \
            [e](const std::pair<ENUM_TYPE, BasicJsonType> &ej_pair) -> bool {  \
                return ej_pair.first == e;                                     \
            });                                                                \
        if (it == std::end(m))                                                 \
        {                                                                      \
            LOGGER__ERROR("Unknown enum value received for " #ENUM_TYPE);      \
            throw std::invalid_argument(                                       \
                "Unknown enum value received for " #ENUM_TYPE);                \
        }                                                                      \
        j = it->second;                                                        \
    }                                                                          \
    template <typename BasicJsonType>                                          \
    inline void from_json(const BasicJsonType &j, ENUM_TYPE &e)                \
    {                                                                          \
        static_assert(std::is_enum<ENUM_TYPE>::value,                          \
                      #ENUM_TYPE " must be an enum!");                         \
        static const std::pair<ENUM_TYPE, BasicJsonType> m[] = __VA_ARGS__;    \
        auto it = std::find_if(                                                \
            std::begin(m), std::end(m),                                        \
            [&j](const std::pair<ENUM_TYPE, BasicJsonType> &ej_pair) -> bool { \
                return ej_pair.second == j;                                    \
            });                                                                \
        if (it == std::end(m))                                                 \
        {                                                                      \
            LOGGER__ERROR("Unknown enum value received for " #ENUM_TYPE);      \
            throw std::invalid_argument(                                       \
                "Unknown enum value received for " #ENUM_TYPE);                \
        }                                                                      \
        e = it->first;                                                         \
    }

//------------------------ enums ------------------------

MEDIALIB_JSON_SERIALIZE_ENUM(
    dsp_interpolation_type_t,
    {
        {INTERPOLATION_TYPE_NEAREST_NEIGHBOR,
         "INTERPOLATION_TYPE_NEAREST_NEIGHBOR"},
        {INTERPOLATION_TYPE_BILINEAR, "INTERPOLATION_TYPE_BILINEAR"},
        {INTERPOLATION_TYPE_AREA, "INTERPOLATION_TYPE_AREA"},
        {INTERPOLATION_TYPE_BICUBIC, "INTERPOLATION_TYPE_BICUBIC"},
    })

MEDIALIB_JSON_SERIALIZE_ENUM(
    flip_direction_t,
    {
        {FLIP_DIRECTION_NONE, "FLIP_DIRECTION_NONE"},
        {FLIP_DIRECTION_HORIZONTAL, "FLIP_DIRECTION_HORIZONTAL"},
        {FLIP_DIRECTION_VERTICAL, "FLIP_DIRECTION_VERTICAL"},
        {FLIP_DIRECTION_BOTH, "FLIP_DIRECTION_BOTH"},
    })

MEDIALIB_JSON_SERIALIZE_ENUM(dsp_image_format_t,
                             {
                                 {DSP_IMAGE_FORMAT_GRAY8, "IMAGE_FORMAT_GRAY8"},
                                 {DSP_IMAGE_FORMAT_RGB, "IMAGE_FORMAT_RGB"},
                                 {DSP_IMAGE_FORMAT_NV12, "IMAGE_FORMAT_NV12"},
                                 {DSP_IMAGE_FORMAT_A420, "IMAGE_FORMAT_A420"},
                             })

MEDIALIB_JSON_SERIALIZE_ENUM(rotation_angle_t,
                             {
                                 {ROTATION_ANGLE_0, "ROTATION_ANGLE_0"},
                                 {ROTATION_ANGLE_90, "ROTATION_ANGLE_90"},
                                 {ROTATION_ANGLE_180, "ROTATION_ANGLE_180"},
                                 {ROTATION_ANGLE_270, "ROTATION_ANGLE_270"},
                             })

MEDIALIB_JSON_SERIALIZE_ENUM(camera_type_t,
                             {
                                 {CAMERA_TYPE_FISHEYE, "CAMERA_TYPE_FISHEYE"},
                                 {CAMERA_TYPE_PINHOLE, "CAMERA_TYPE_PINHOLE"},
                                 {CAMERA_TYPE_INPUT_DISTORTIONS,
                                  "CAMERA_TYPE_INPUT_DISTORTIONS"},
                             })

MEDIALIB_JSON_SERIALIZE_ENUM(
    digital_zoom_mode_t,
    {
        {DIGITAL_ZOOM_MODE_ROI, "DIGITAL_ZOOM_MODE_ROI"},
        {DIGITAL_ZOOM_MODE_MAGNIFICATION, "DIGITAL_ZOOM_MODE_MAGNIFICATION"},
    })

//------------------------ roi_t ------------------------

void to_json(nlohmann::json &j, const roi_t &roi)
{
    j = nlohmann::json{
        {"x", roi.x},
        {"y", roi.y},
        {"width", roi.width},
        {"height", roi.height},
    };
}

void from_json(const nlohmann::json &j, roi_t &roi)
{
    j.at("x").get_to(roi.x);
    j.at("y").get_to(roi.y);
    j.at("width").get_to(roi.width);
    j.at("height").get_to(roi.height);
}

//------------------------ dewarp_config_t ------------------------

void to_json(nlohmann::json &j, const dewarp_config_t &dewarp)
{
    j = nlohmann::json{
        {"enabled", dewarp.enabled},
        {"sensor_calib_path", dewarp.sensor_calib_path},
        {"color_interpolation", dewarp.interpolation_type},
        {"camera_type", dewarp.camera_type},
        {"camera_fov", dewarp.camera_fov},
    };
}

void from_json(const nlohmann::json &j, dewarp_config_t &dewarp)
{
    j.at("enabled").get_to(dewarp.enabled);
    j.at("sensor_calib_path").get_to(dewarp.sensor_calib_path);
    j.at("color_interpolation").get_to(dewarp.interpolation_type);
    j.at("camera_type").get_to(dewarp.camera_type);
    j.at("camera_fov").get_to(dewarp.camera_fov);
}

//------------------------ dis_debug_config_t ------------------------

void to_json(nlohmann::json &j, const dis_debug_config_t &dis_debug)
{
    j = nlohmann::json{
        {"generate_resize_grid", dis_debug.generate_resize_grid},
        {"fix_stabilization", dis_debug.fix_stabilization},
        {"fix_stabilization_longitude", dis_debug.fix_stabilization_longitude},
        {"fix_stabilization_latitude", dis_debug.fix_stabilization_latitude},
    };
}

void from_json(const nlohmann::json &j, dis_debug_config_t &dis_debug)
{
    j.at("generate_resize_grid").get_to(dis_debug.generate_resize_grid);
    j.at("fix_stabilization").get_to(dis_debug.fix_stabilization);
    j.at("fix_stabilization_longitude")
        .get_to(dis_debug.fix_stabilization_longitude);
    j.at("fix_stabilization_latitude")
        .get_to(dis_debug.fix_stabilization_latitude);
}

//------------------------ dis_config_t ------------------------

void to_json(nlohmann::json &j, const dis_config_t &dis)
{
    j = nlohmann::json{
        {"enabled", dis.enabled},
        {"minimun_coefficient_filter", dis.minimun_coefficient_filter},
        {"decrement_coefficient_threshold",
         dis.decrement_coefficient_threshold},
        {"increment_coefficient_threshold",
         dis.increment_coefficient_threshold},
        {"running_average_coefficient", dis.running_average_coefficient},
        {"std_multiplier", dis.std_multiplier},
        {"black_corners_correction_enabled",
         dis.black_corners_correction_enabled},
        {"black_corners_threshold", dis.black_corners_threshold},
        {"debug", dis.debug},
    };
}

void from_json(const nlohmann::json &j, dis_config_t &dis)
{
    j.at("enabled").get_to(dis.enabled);
    j.at("minimun_coefficient_filter").get_to(dis.minimun_coefficient_filter);
    j.at("decrement_coefficient_threshold")
        .get_to(dis.decrement_coefficient_threshold);
    j.at("increment_coefficient_threshold")
        .get_to(dis.increment_coefficient_threshold);
    j.at("running_average_coefficient").get_to(dis.running_average_coefficient);
    j.at("std_multiplier").get_to(dis.std_multiplier);
    j.at("black_corners_correction_enabled")
        .get_to(dis.black_corners_correction_enabled);
    j.at("black_corners_threshold").get_to(dis.black_corners_threshold);
    j.at("debug").get_to(dis.debug);
}

//------------------------ digital_zoom_config_t ------------------------

void to_json(nlohmann::json &j, const digital_zoom_config_t &dz_conf)
{
    j = nlohmann::json{
        {"enabled", dz_conf.enabled},
        {"mode", dz_conf.mode},
        {"magnification", dz_conf.magnification},
        {"roi", dz_conf.roi},
    };
}

void from_json(const nlohmann::json &j, digital_zoom_config_t &dz_conf)
{
    j.at("enabled").get_to(dz_conf.enabled);
    j.at("mode").get_to(dz_conf.mode);
    j.at("magnification").get_to(dz_conf.magnification);
    j.at("roi").get_to(dz_conf.roi);
}

//------------------------ flip_config_t ------------------------

void to_json(nlohmann::json &j, const flip_config_t &flip_conf)
{
    j = nlohmann::json{
        {"enabled", flip_conf.enabled},
        {"direction", flip_conf.direction},
    };
}

void from_json(const nlohmann::json &j, flip_config_t &flip_conf)
{
    j.at("enabled").get_to(flip_conf.enabled);
    j.at("direction").get_to(flip_conf.direction);
}

//------------------------ rotation_config_t ------------------------

void to_json(nlohmann::json &j, const rotation_config_t &r_conf)
{
    j = nlohmann::json{
        {"enabled", r_conf.enabled},
        {"angle", r_conf.angle},
    };
}

void from_json(const nlohmann::json &j, rotation_config_t &r_conf)
{
    j.at("enabled").get_to(r_conf.enabled);
    j.at("angle").get_to(r_conf.angle);
}

//------------------------ output_resolution_t ------------------------

void to_json(nlohmann::json &j, const output_resolution_t &out_res)
{
    j = nlohmann::json{
        {"framerate", out_res.framerate},
        {"width", out_res.dimensions.destination_width},
        {"height", out_res.dimensions.destination_height},
        {"pool_max_buffers", out_res.pool_max_buffers},
    };
}

void from_json(const nlohmann::json &j, output_resolution_t &out_res)
{
    j.at("framerate").get_to(out_res.framerate);
    j.at("width").get_to(out_res.dimensions.destination_width);
    j.at("height").get_to(out_res.dimensions.destination_height);
    j.at("pool_max_buffers").get_to(out_res.pool_max_buffers);
    out_res.dimensions.perform_crop = false;
}

//------------------------ output_video_config_t ------------------------

void to_json(nlohmann::json &j, const output_video_config_t &out_conf)
{
    j = nlohmann::json{
        {"method", out_conf.interpolation_type},
        {"format", out_conf.format},
        {"resolutions", out_conf.resolutions},
        {"grayscale", out_conf.grayscale},
    };
}

void from_json(const nlohmann::json &j, output_video_config_t &out_conf)
{
    j.at("method").get_to(out_conf.interpolation_type);
    j.at("format").get_to(out_conf.format);
    j.at("resolutions").get_to(out_conf.resolutions);
    j.at("grayscale").get_to(out_conf.grayscale);
}

//------------------------ input_video_config_t ------------------------

void to_json(nlohmann::json &j, const input_video_config_t &in_conf)
{
    j = nlohmann::json{
        {"format", in_conf.format},
        {"source", in_conf.video_device},
        {"resolution", in_conf.resolution},
    };
}

void from_json(const nlohmann::json &j, input_video_config_t &in_conf)
{
    j.at("format").get_to(in_conf.format);
    j.at("source").get_to(in_conf.video_device);
    j.at("resolution").get_to(in_conf.resolution);
}

//------------------------ pre_proc_op_configurations ------------------------

void to_json(nlohmann::json &j, const pre_proc_op_configurations &pp_conf)
{
    j = nlohmann::json{
        {"input_stream", pp_conf.input_video_config},
        {"output_video", pp_conf.output_video_config},
        {"dewarp", pp_conf.dewarp_config},
        {"dis", pp_conf.dis_config},
        {"digital_zoom", pp_conf.digital_zoom_config},
        {"rotation", pp_conf.rotation_config},
        {"flip", pp_conf.flip_config},
    };
}

void from_json(const nlohmann::json &j, pre_proc_op_configurations &pp_conf)
{
    j.at("input_stream").get_to(pp_conf.input_video_config);
    j.at("output_video").get_to(pp_conf.output_video_config);
    j.at("dewarp").get_to(pp_conf.dewarp_config);
    j.at("dis").get_to(pp_conf.dis_config);
    j.at("digital_zoom").get_to(pp_conf.digital_zoom_config);
    j.at("rotation").get_to(pp_conf.rotation_config);
    j.at("flip").get_to(pp_conf.flip_config);
}