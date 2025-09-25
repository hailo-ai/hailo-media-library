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

#pragma once
#include <iostream>
#include <nlohmann/json-schema.hpp>
#include "media_library_types.hpp"
#include "media_library_logger.hpp"
#include "encoder_config.hpp"

using json = nlohmann::json;
#define MODULE_NAME LoggerType::Config

#define MEDIALIB_JSON_SERIALIZE_ENUM(ENUM_TYPE, ...)                                                                   \
    template <typename BasicJsonType> inline void to_json(BasicJsonType &j, const ENUM_TYPE &e)                        \
    {                                                                                                                  \
        static_assert(std::is_enum<ENUM_TYPE>::value, #ENUM_TYPE " must be an enum!");                                 \
        static const std::pair<ENUM_TYPE, BasicJsonType> m[] = __VA_ARGS__;                                            \
        auto it =                                                                                                      \
            std::find_if(std::begin(m), std::end(m), [e](const std::pair<ENUM_TYPE, BasicJsonType> &ej_pair) -> bool { \
                return ej_pair.first == e;                                                                             \
            });                                                                                                        \
        if (it == std::end(m))                                                                                         \
        {                                                                                                              \
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown enum value received for " #ENUM_TYPE);                         \
            throw std::invalid_argument("Unknown enum value received for " #ENUM_TYPE);                                \
        }                                                                                                              \
        j = it->second;                                                                                                \
    }                                                                                                                  \
    template <typename BasicJsonType> inline void from_json(const BasicJsonType &j, ENUM_TYPE &e)                      \
    {                                                                                                                  \
        static_assert(std::is_enum<ENUM_TYPE>::value, #ENUM_TYPE " must be an enum!");                                 \
        static const std::pair<ENUM_TYPE, BasicJsonType> m[] = __VA_ARGS__;                                            \
        auto it = std::find_if(                                                                                        \
            std::begin(m), std::end(m),                                                                                \
            [&j](const std::pair<ENUM_TYPE, BasicJsonType> &ej_pair) -> bool { return ej_pair.second == j; });         \
        if (it == std::end(m))                                                                                         \
        {                                                                                                              \
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown enum value received for " #ENUM_TYPE);                         \
            throw std::invalid_argument("Unknown enum value received for " #ENUM_TYPE);                                \
        }                                                                                                              \
        e = it->first;                                                                                                 \
    }

//------------------------ enums ------------------------

MEDIALIB_JSON_SERIALIZE_ENUM(dsp_interpolation_type_t,
                             {
                                 {INTERPOLATION_TYPE_NEAREST_NEIGHBOR, "INTERPOLATION_TYPE_NEAREST_NEIGHBOR"},
                                 {INTERPOLATION_TYPE_BILINEAR, "INTERPOLATION_TYPE_BILINEAR"},
                                 {INTERPOLATION_TYPE_AREA, "INTERPOLATION_TYPE_AREA"},
                                 {INTERPOLATION_TYPE_BICUBIC, "INTERPOLATION_TYPE_BICUBIC"},
                             })

MEDIALIB_JSON_SERIALIZE_ENUM(flip_direction_t, {
                                                   {FLIP_DIRECTION_NONE, "FLIP_DIRECTION_NONE"},
                                                   {FLIP_DIRECTION_HORIZONTAL, "FLIP_DIRECTION_HORIZONTAL"},
                                                   {FLIP_DIRECTION_VERTICAL, "FLIP_DIRECTION_VERTICAL"},
                                                   {FLIP_DIRECTION_BOTH, "FLIP_DIRECTION_BOTH"},
                                               })

MEDIALIB_JSON_SERIALIZE_ENUM(HailoFormat, {
                                              {HAILO_FORMAT_GRAY8, "IMAGE_FORMAT_GRAY8"},
                                              {HAILO_FORMAT_RGB, "IMAGE_FORMAT_RGB"},
                                              {HAILO_FORMAT_NV12, "IMAGE_FORMAT_NV12"},
                                              {HAILO_FORMAT_A420, "IMAGE_FORMAT_A420"},
                                          })

MEDIALIB_JSON_SERIALIZE_ENUM(rotation_angle_t, {
                                                   {ROTATION_ANGLE_0, "ROTATION_ANGLE_0"},
                                                   {ROTATION_ANGLE_90, "ROTATION_ANGLE_90"},
                                                   {ROTATION_ANGLE_180, "ROTATION_ANGLE_180"},
                                                   {ROTATION_ANGLE_270, "ROTATION_ANGLE_270"},
                                               })

MEDIALIB_JSON_SERIALIZE_ENUM(camera_type_t, {
                                                {CAMERA_TYPE_FISHEYE, "CAMERA_TYPE_FISHEYE"},
                                                {CAMERA_TYPE_PINHOLE, "CAMERA_TYPE_PINHOLE"},
                                                {CAMERA_TYPE_INPUT_DISTORTIONS, "CAMERA_TYPE_INPUT_DISTORTIONS"},
                                            })

MEDIALIB_JSON_SERIALIZE_ENUM(digital_zoom_mode_t,
                             {
                                 {DIGITAL_ZOOM_MODE_ROI, "DIGITAL_ZOOM_MODE_ROI"},
                                 {DIGITAL_ZOOM_MODE_MAGNIFICATION, "DIGITAL_ZOOM_MODE_MAGNIFICATION"},
                             })

MEDIALIB_JSON_SERIALIZE_ENUM(denoise_method_t, {
                                                   {DENOISE_METHOD_VD1, "HIGH_QUALITY"},
                                                   {DENOISE_METHOD_VD2, "BALANCED"},
                                                   {DENOISE_METHOD_VD3, "HIGH_PERFORMANCE"},
                                               })

MEDIALIB_JSON_SERIALIZE_ENUM(codec_t, {
                                          {CODEC_TYPE_H264, "CODEC_TYPE_H264"},
                                          {CODEC_TYPE_HEVC, "CODEC_TYPE_HEVC"},
                                      })

MEDIALIB_JSON_SERIALIZE_ENUM(rc_mode_t, {
                                            {VBR, "VBR"},
                                            {CVBR, "CVBR"},
                                            {HRD, "HRD"},
                                            {CQP, "CQP"},
                                        })

MEDIALIB_JSON_SERIALIZE_ENUM(deblocking_filter_type_t, {
                                                           {DEBLOCKING_FILTER_ENABLED, "DEBLOCKING_FILTER_ENABLED"},
                                                           {DEBLOCKING_FILTER_DISABLED, "DEBLOCKING_FILTER_DISABLED"},
                                                           {DEBLOCKING_FILTER_DISABLED_ON_SLICE_EDGES,
                                                            "DEBLOCKING_FILTER_DISABLED_ON_SLICE_EDGES"},
                                                       })

MEDIALIB_JSON_SERIALIZE_ENUM(hdr_resolution_t, {
                                                   {HDR_RESOLUTION_FHD, "fhd"},
                                                   {HDR_RESOLUTION_4K, "4k"},
                                               })

MEDIALIB_JSON_SERIALIZE_ENUM(hdr_dol_t, {
                                            {HDR_DOL_2, 2},
                                            {HDR_DOL_3, 3},
                                        })

MEDIALIB_JSON_SERIALIZE_ENUM(zoom_bitrate_adjuster_mode_t,
                             {
                                 {ZOOM_BITRATE_ADJUSTER_DISABLED, "DISABLED"},
                                 {ZOOM_BITRATE_ADJUSTER_ZOOMING_PROCESS, "ZOOMING_PROCESS"},
                                 {ZOOM_BITRATE_ADJUSTER_ZOOM_LEVEL, "ZOOM_LEVEL"},
                                 {ZOOM_BITRATE_ADJUSTER_BOTH, "BOTH"},
                             })

MEDIALIB_JSON_SERIALIZE_ENUM(motion_detection_sensitivity_levels_t, {
                                                                        {LOWEST, "LOWEST"},
                                                                        {LOW, "LOW"},
                                                                        {MEDIUM, "MEDIUM"},
                                                                        {HIGH, "HIGH"},
                                                                        {HIGHEST, "HIGHEST"},
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
    };
}

void from_json(const nlohmann::json &j, dewarp_config_t &dewarp)
{
    j.at("enabled").get_to(dewarp.enabled);
    j.at("sensor_calib_path").get_to(dewarp.sensor_calib_path);
    j.at("color_interpolation").get_to(dewarp.interpolation_type);
    j.at("camera_type").get_to(dewarp.camera_type);
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
    j.at("fix_stabilization_longitude").get_to(dis_debug.fix_stabilization_longitude);
    j.at("fix_stabilization_latitude").get_to(dis_debug.fix_stabilization_latitude);
}

//------------------------angular_dis----------------------------

void to_json(nlohmann::json &j, const angular_dis_vsm_config_t &vsm_conf)
{
    j = nlohmann::json{
        {"hoffset", vsm_conf.hoffset},
        {"voffset", vsm_conf.voffset},
        {"width", vsm_conf.width},
        {"height", vsm_conf.height},
        {"max_displacement", vsm_conf.max_displacement},
    };
}

void from_json(const nlohmann::json &j, angular_dis_vsm_config_t &vsm_conf)
{
    j.at("hoffset").get_to(vsm_conf.hoffset);
    j.at("voffset").get_to(vsm_conf.voffset);
    j.at("width").get_to(vsm_conf.width);
    j.at("height").get_to(vsm_conf.height);
    j.at("max_displacement").get_to(vsm_conf.max_displacement);
}

void to_json(nlohmann::json &j, const angular_dis_config_t &ad_conf)
{
    j = nlohmann::json{
        {"enabled", ad_conf.enabled},
        {"vsm", ad_conf.vsm_config},
    };
}

void from_json(const nlohmann::json &j, angular_dis_config_t &ad_conf)
{
    j.at("enabled").get_to(ad_conf.enabled);
    j.at("vsm").get_to(ad_conf.vsm_config);
}

//------------------------encoder_config_t ------------------------

void to_json(nlohmann::json &j, const input_config_t &in_conf)
{
    j = nlohmann::json{
        {"width", in_conf.width},
        {"height", in_conf.height},
        {"framerate", in_conf.framerate},
        {"format", in_conf.format},
    };
}

void from_json(const nlohmann::json &j, input_config_t &in_conf)
{
    j.at("width").get_to(in_conf.width);
    j.at("height").get_to(in_conf.height);
    j.at("framerate").get_to(in_conf.framerate);
    j.at("format").get_to(in_conf.format);
}

void to_json(nlohmann::json &j, const output_config_t &out_conf)
{
    j = nlohmann::json{{"codec", out_conf.codec}};

    if (out_conf.profile != std::nullopt)
    {
        j["profile"] = out_conf.profile.value();
    }
    if (out_conf.level != std::nullopt)
    {
        j["level"] = out_conf.level.value();
    }
}

void from_json(const nlohmann::json &j, output_config_t &out_conf)
{
    j.at("codec").get_to(out_conf.codec);

    if (j.count("profile") != 0)
    {
        out_conf.profile = j.at("profile").get<std::string>();
    }
    if (j.count("level") != 0)
    {
        out_conf.level = j.at("level").get<std::string>();
    }
}

void to_json(nlohmann::json &j, const gop_config_t &gop_conf)
{
    j = nlohmann::json{
        {"gop_size", gop_conf.gop_size},
        {"b_frame_qp_delta", gop_conf.b_frame_qp_delta},
    };
}

void from_json(const nlohmann::json &j, gop_config_t &gop_conf)
{
    j.at("gop_size").get_to(gop_conf.gop_size);
    j.at("b_frame_qp_delta").get_to(gop_conf.b_frame_qp_delta);
}

void to_json(nlohmann::json &j, const deblocking_filter_t &df_conf)
{
    j = nlohmann::json{
        {"type", df_conf.type},
        {"tc_offset", df_conf.tc_offset},
        {"beta_offset", df_conf.beta_offset},
        {"deblock_override", df_conf.deblock_override},
    };
}

void from_json(const nlohmann::json &j, deblocking_filter_t &df_conf)
{
    j.at("type").get_to(df_conf.type);
    j.at("tc_offset").get_to(df_conf.tc_offset);
    j.at("beta_offset").get_to(df_conf.beta_offset);
    j.at("deblock_override").get_to(df_conf.deblock_override);
}

void to_json(nlohmann::json &j, const coding_roi_area_t &roi_conf)
{
    j = nlohmann::json{
        {"enable", roi_conf.enable}, {"top", roi_conf.top},     {"left", roi_conf.left},
        {"bottom", roi_conf.bottom}, {"right", roi_conf.right}, {"qp_delta", roi_conf.qp_delta},
    };
}

void from_json(const nlohmann::json &j, coding_roi_area_t &roi_conf)
{
    j.at("enable").get_to(roi_conf.enable);
    j.at("top").get_to(roi_conf.top);
    j.at("left").get_to(roi_conf.left);
    j.at("bottom").get_to(roi_conf.bottom);
    j.at("right").get_to(roi_conf.right);
    j.at("qp_delta").get_to(roi_conf.qp_delta);
}

void to_json(nlohmann::json &j, const coding_roi_t &intra_conf)
{
    j = nlohmann::json{
        {"enable", intra_conf.enable}, {"top", intra_conf.top},     {"left", intra_conf.left},
        {"bottom", intra_conf.bottom}, {"right", intra_conf.right},
    };
}

void from_json(const nlohmann::json &j, coding_roi_t &intra_conf)
{
    j.at("enable").get_to(intra_conf.enable);
    j.at("top").get_to(intra_conf.top);
    j.at("left").get_to(intra_conf.left);
    j.at("bottom").get_to(intra_conf.bottom);
    j.at("right").get_to(intra_conf.right);
}

void to_json(nlohmann::json &j, const coding_control_config_t &cc_conf)
{
    j = nlohmann::json{
        {"sei_messages", cc_conf.sei_messages}, {"deblocking_filter", cc_conf.deblocking_filter},
        {"intra_area", cc_conf.intra_area},     {"ipcm_area1", cc_conf.ipcm_area1},
        {"ipcm_area2", cc_conf.ipcm_area2},     {"roi_area1", cc_conf.roi_area1},
        {"roi_area2", cc_conf.roi_area2},
    };
}

void from_json(const nlohmann::json &j, coding_control_config_t &cc_conf)
{
    j.at("sei_messages").get_to(cc_conf.sei_messages);
    j.at("deblocking_filter").get_to(cc_conf.deblocking_filter);
    j.at("intra_area").get_to(cc_conf.intra_area);
    j.at("ipcm_area1").get_to(cc_conf.ipcm_area1);
    j.at("ipcm_area2").get_to(cc_conf.ipcm_area2);
    j.at("roi_area1").get_to(cc_conf.roi_area1);
    j.at("roi_area2").get_to(cc_conf.roi_area2);
}

void to_json(nlohmann::json &j, const bitrate_config_t &bitrate_conf)
{
    j = nlohmann::json{
        {"target_bitrate", bitrate_conf.target_bitrate},
    };

    if (bitrate_conf.bit_var_range_i != std::nullopt)
    {
        j["bit_var_range_i"] = bitrate_conf.bit_var_range_i.value();
    }
    if (bitrate_conf.bit_var_range_p != std::nullopt)
    {
        j["bit_var_range_p"] = bitrate_conf.bit_var_range_p.value();
    }
    if (bitrate_conf.bit_var_range_b != std::nullopt)
    {
        j["bit_var_range_b"] = bitrate_conf.bit_var_range_b.value();
    }
    if (bitrate_conf.tolerance_moving_bitrate != std::nullopt)
    {
        j["tolerance_moving_bitrate"] = bitrate_conf.tolerance_moving_bitrate.value();
    }
    if (bitrate_conf.variation != std::nullopt)
    {
        j["variation"] = bitrate_conf.variation.value();
    }
}

void from_json(const nlohmann::json &j, bitrate_config_t &bitrate_conf)
{
    j.at("target_bitrate").get_to(bitrate_conf.target_bitrate);

    if (j.count("bit_var_range_i") != 0)
    {
        bitrate_conf.bit_var_range_i = j.at("bit_var_range_i").get<uint32_t>();
    }
    if (j.count("bit_var_range_p") != 0)
    {
        bitrate_conf.bit_var_range_p = j.at("bit_var_range_p").get<uint32_t>();
    }
    if (j.count("bit_var_range_b") != 0)
    {
        bitrate_conf.bit_var_range_b = j.at("bit_var_range_b").get<uint32_t>();
    }
    if (j.count("tolerance_moving_bitrate") != 0)
    {
        bitrate_conf.tolerance_moving_bitrate = j.at("tolerance_moving_bitrate").get<uint32_t>();
    }
    if (j.count("variation") != 0)
    {
        bitrate_conf.variation = j.at("variation").get<uint32_t>();
    }
}

void to_json(nlohmann::json &j, const quantization_config_t &q_conf)
{
    j = nlohmann::json{
        {"qp_hdr", q_conf.qp_hdr},
    };

    if (q_conf.qp_min != std::nullopt)
    {
        j["qp_min"] = q_conf.qp_min.value();
    }
    if (q_conf.qp_max != std::nullopt)
    {
        j["qp_max"] = q_conf.qp_max.value();
    }
    if (q_conf.intra_qp_delta != std::nullopt)
    {
        j["intra_qp_delta"] = q_conf.intra_qp_delta.value();
    }
    if (q_conf.fixed_intra_qp != std::nullopt)
    {
        j["fixed_intra_qp"] = q_conf.fixed_intra_qp.value();
    }
}

void from_json(const nlohmann::json &j, quantization_config_t &q_conf)
{
    j.at("qp_hdr").get_to(q_conf.qp_hdr);

    if (j.count("qp_min") != 0)
    {
        q_conf.qp_min = j.at("qp_min").get<uint32_t>();
    }
    if (j.count("qp_max") != 0)
    {
        q_conf.qp_max = j.at("qp_max").get<uint32_t>();
    }
    if (j.count("intra_qp_delta") != 0)
    {
        q_conf.intra_qp_delta = j.at("intra_qp_delta").get<int32_t>();
    }
    if (j.count("fixed_intra_qp") != 0)
    {
        q_conf.fixed_intra_qp = j.at("fixed_intra_qp").get<uint32_t>();
    }
}

void to_json(nlohmann::json &j, const qp_smooth_settings_t &qp_conf)
{
    j = nlohmann::json{};

    if (qp_conf.qp_delta != std::nullopt)
    {
        j["qp_delta"] = qp_conf.qp_delta.value();
    }
    if (qp_conf.qp_delta_limit != std::nullopt)
    {
        j["qp_delta_limit"] = qp_conf.qp_delta_limit.value();
    }
    if (qp_conf.qp_delta_step != std::nullopt)
    {
        j["qp_delta_step"] = qp_conf.qp_delta_step.value();
    }
    if (qp_conf.qp_delta_limit_step != std::nullopt)
    {
        j["qp_delta_limit_step"] = qp_conf.qp_delta_limit_step.value();
    }
    if (qp_conf.alpha != std::nullopt)
    {
        j["alpha"] = qp_conf.alpha.value();
    }
    if (qp_conf.q_step_divisor != std::nullopt)
    {
        j["q_step_divisor"] = qp_conf.q_step_divisor.value();
    }
}

void from_json(const nlohmann::json &j, qp_smooth_settings_t &qp_conf)
{
    if (j.count("qp_delta") != 0)
    {
        qp_conf.qp_delta = j.at("qp_delta").get<int32_t>();
    }
    if (j.count("qp_delta_limit") != 0)
    {
        qp_conf.qp_delta_limit = j.at("qp_delta_limit").get<int32_t>();
    }
    if (j.count("qp_delta_step") != 0)
    {
        qp_conf.qp_delta_step = j.at("qp_delta_step").get<uint32_t>();
    }
    if (j.count("qp_delta_limit_step") != 0)
    {
        qp_conf.qp_delta_limit_step = j.at("qp_delta_limit_step").get<uint32_t>();
    }
    if (j.count("alpha") != 0)
    {
        qp_conf.alpha = j.at("alpha").get<float>();
    }
    if (j.count("q_step_divisor") != 0)
    {
        qp_conf.q_step_divisor = j.at("q_step_divisor").get<int32_t>();
    }
}

void to_json(nlohmann::json &j, const gop_anomaly_bitrate_adjuster_t &bitrate_conf)
{
    j = nlohmann::json{};

    if (bitrate_conf.enable != std::nullopt)
    {
        j["enable"] = bitrate_conf.enable.value();
    }
    if (bitrate_conf.threshold_high != std::nullopt)
    {
        j["threshold_high"] = bitrate_conf.threshold_high.value();
    }
    if (bitrate_conf.threshold_low != std::nullopt)
    {
        j["threshold_low"] = bitrate_conf.threshold_low.value();
    }
    if (bitrate_conf.max_target_bitrate_factor != std::nullopt)
    {
        j["max_target_bitrate_factor"] = bitrate_conf.max_target_bitrate_factor.value();
    }
    if (bitrate_conf.adjustment_factor != std::nullopt)
    {
        j["adjustment_factor"] = bitrate_conf.adjustment_factor.value();
    }
}

void from_json(const nlohmann::json &j, gop_anomaly_bitrate_adjuster_t &bitrate_conf)
{
    if (j.count("enable") != 0)
    {
        bitrate_conf.enable = j.at("enable").get<bool>();
    }
    if (j.count("threshold_high") != 0)
    {
        bitrate_conf.threshold_high = j.at("threshold_high").get<float>();
    }
    if (j.count("threshold_low") != 0)
    {
        bitrate_conf.threshold_low = j.at("threshold_low").get<float>();
    }
    if (j.count("max_target_bitrate_factor") != 0)
    {
        bitrate_conf.max_target_bitrate_factor = j.at("max_target_bitrate_factor").get<float>();
    }
    if (j.count("adjustment_factor") != 0)
    {
        bitrate_conf.adjustment_factor = j.at("adjustment_factor").get<float>();
    }
}

void to_json(nlohmann::json &j, const zoom_bitrate_adjuster_t &zoom_conf)
{
    j = nlohmann::json{};

    if (zoom_conf.mode != std::nullopt)
    {
        j["mode"] = zoom_conf.mode.value();
    }
    if (zoom_conf.zooming_process_bitrate_factor != std::nullopt)
    {
        j["zooming_process_bitrate_factor"] = zoom_conf.zooming_process_bitrate_factor.value();
    }
    if (zoom_conf.zooming_process_timeout_ms != std::nullopt)
    {
        j["zooming_process_timeout_ms"] = zoom_conf.zooming_process_timeout_ms.value();
    }
    if (zoom_conf.zooming_process_max_bitrate != std::nullopt)
    {
        j["zooming_process_max_bitrate"] = zoom_conf.zooming_process_max_bitrate.value();
    }
    if (zoom_conf.zooming_process_force_keyframe != std::nullopt)
    {
        j["zooming_process_force_keyframe"] = zoom_conf.zooming_process_force_keyframe.value();
    }
    if (zoom_conf.zoom_level_threshold != std::nullopt)
    {
        j["zoom_level_threshold"] = zoom_conf.zoom_level_threshold.value();
    }
    if (zoom_conf.zoom_level_bitrate_factor != std::nullopt)
    {
        j["zoom_level_bitrate_factor"] = zoom_conf.zoom_level_bitrate_factor.value();
    }
}

void from_json(const nlohmann::json &j, zoom_bitrate_adjuster_t &zoom_conf)
{
    if (j.count("mode") != 0)
    {
        zoom_conf.mode = j.at("mode").get<zoom_bitrate_adjuster_mode_t>();
    }
    if (j.count("zooming_process_bitrate_factor") != 0)
    {
        zoom_conf.zooming_process_bitrate_factor = j.at("zooming_process_bitrate_factor").get<float>();
    }
    if (j.count("zooming_process_timeout_ms") != 0)
    {
        zoom_conf.zooming_process_timeout_ms = j.at("zooming_process_timeout_ms").get<uint32_t>();
    }
    if (j.count("zooming_process_max_bitrate") != 0)
    {
        zoom_conf.zooming_process_max_bitrate = j.at("zooming_process_max_bitrate").get<uint32_t>();
    }
    if (j.count("zooming_process_force_keyframe") != 0)
    {
        zoom_conf.zooming_process_force_keyframe = j.at("zooming_process_force_keyframe").get<bool>();
    }
    if (j.count("zoom_level_threshold") != 0)
    {
        zoom_conf.zoom_level_threshold = j.at("zoom_level_threshold").get<float>();
    }
    if (j.count("zoom_level_bitrate_factor") != 0)
    {
        zoom_conf.zoom_level_bitrate_factor = j.at("zoom_level_bitrate_factor").get<float>();
    }
}

void to_json(nlohmann::json &j, const rate_control_config_t &rc_conf)
{
    j = nlohmann::json{
        {"rc_mode", rc_conf.rc_mode},
        {"picture_rc", rc_conf.picture_rc},
        {"picture_skip", rc_conf.picture_skip},
        {"intra_pic_rate", rc_conf.intra_pic_rate},
        {"bitrate", rc_conf.bitrate},
        {"quantization", rc_conf.quantization},
        {"zoom_bitrate_adjuster", rc_conf.zoom_bitrate_adjuster},
        {"qp_smooth_settings", rc_conf.qp_smooth_settings},
        {"gop_anomaly_bitrate_adjuster", rc_conf.gop_anomaly_bitrate_adjuster},
    };

    if (rc_conf.ctb_rc != std::nullopt)
    {
        j["ctb_rc"] = rc_conf.ctb_rc.value();
    }
    if (rc_conf.hrd != std::nullopt)
    {
        j["hrd"] = rc_conf.hrd.value();
    }
    if (rc_conf.gop_length != std::nullopt)
    {
        j["gop_length"] = rc_conf.gop_length.value();
    }
    if (rc_conf.monitor_frames != std::nullopt)
    {
        j["monitor_frames"] = rc_conf.monitor_frames.value();
    }
    if (rc_conf.cvbr != std::nullopt)
    {
        j["cvbr"] = rc_conf.cvbr.value();
    }
    if (rc_conf.padding != std::nullopt)
    {
        j["padding"] = rc_conf.padding.value();
    }
    if (rc_conf.hrd_cpb_size != std::nullopt)
    {
        j["hrd_cpb_size"] = rc_conf.hrd_cpb_size.value();
    }
    if (rc_conf.block_rc_size != std::nullopt)
    {
        j["block_rc_size"] = rc_conf.block_rc_size.value();
    }
}

void from_json(const nlohmann::json &j, rate_control_config_t &rc_conf)
{
    j.at("rc_mode").get_to(rc_conf.rc_mode);
    j.at("picture_rc").get_to(rc_conf.picture_rc);
    j.at("picture_skip").get_to(rc_conf.picture_skip);
    j.at("intra_pic_rate").get_to(rc_conf.intra_pic_rate);
    j.at("bitrate").get_to(rc_conf.bitrate);
    j.at("quantization").get_to(rc_conf.quantization);
    j.at("zoom_bitrate_adjuster").get_to(rc_conf.zoom_bitrate_adjuster);
    j.at("qp_smooth_settings").get_to(rc_conf.qp_smooth_settings);
    j.at("gop_anomaly_bitrate_adjuster").get_to(rc_conf.gop_anomaly_bitrate_adjuster);

    if (j.count("ctb_rc") != 0)
    {
        rc_conf.ctb_rc = j.at("ctb_rc").get<bool>();
    }
    if (j.count("hrd") != 0)
    {
        rc_conf.hrd = j.at("hrd").get<bool>();
    }
    if (j.count("gop_length") != 0)
    {
        rc_conf.gop_length = j.at("gop_length").get<uint32_t>();
    }
    if (j.count("monitor_frames") != 0)
    {
        rc_conf.monitor_frames = j.at("monitor_frames").get<uint32_t>();
    }
    if (j.count("cvbr") != 0)
    {
        rc_conf.cvbr = j.at("cvbr").get<uint32_t>();
    }
    if (j.count("padding") != 0)
    {
        rc_conf.padding = j.at("padding").get<bool>();
    }
    if (j.count("hrd_cpb_size") != 0)
    {
        rc_conf.hrd_cpb_size = j.at("hrd_cpb_size").get<uint32_t>();
    }
    if (j.count("block_rc_size") != 0)
    {
        rc_conf.block_rc_size = j.at("block_rc_size").get<uint32_t>();
    }
}

void to_json(nlohmann::json &j, const bitrate_monitor_config_t &bitrate_monitor_conf)
{
    j = nlohmann::json{
        {"enable", bitrate_monitor_conf.enable},
        {"period", bitrate_monitor_conf.period},
        {"result_output_path", bitrate_monitor_conf.result_output_path},
        {"output_result_to_file", bitrate_monitor_conf.output_result_to_file},
    };
}

void from_json(const nlohmann::json &j, bitrate_monitor_config_t &bitrate_monitor_conf)
{
    j.at("enable").get_to(bitrate_monitor_conf.enable);
    j.at("period").get_to(bitrate_monitor_conf.period);
    j.at("result_output_path").get_to(bitrate_monitor_conf.result_output_path);
    j.at("output_result_to_file").get_to(bitrate_monitor_conf.output_result_to_file);
}

void to_json(nlohmann::json &j, const cycle_monitor_config_t &cycle_monitor_conf)
{
    j = nlohmann::json{
        {"enable", cycle_monitor_conf.enable},
        {"start_delay", cycle_monitor_conf.start_delay},
        {"deviation_threshold", cycle_monitor_conf.deviation_threshold},
        {"result_output_path", cycle_monitor_conf.result_output_path},
        {"output_result_to_file", cycle_monitor_conf.output_result_to_file},
    };
}

void from_json(const nlohmann::json &j, cycle_monitor_config_t &cycle_monitor_conf)
{
    j.at("enable").get_to(cycle_monitor_conf.enable);
    j.at("start_delay").get_to(cycle_monitor_conf.start_delay);
    j.at("deviation_threshold").get_to(cycle_monitor_conf.deviation_threshold);
    j.at("result_output_path").get_to(cycle_monitor_conf.result_output_path);
    j.at("output_result_to_file").get_to(cycle_monitor_conf.output_result_to_file);
}

void to_json(nlohmann::json &j, const encoder_monitors_config_t &monitors_conf)
{
    j = nlohmann::json{
        {"bitrate_monitor", monitors_conf.bitrate_monitor},
        {"cycle_monitor", monitors_conf.cycle_monitor},
    };
}

void from_json(const nlohmann::json &j, encoder_monitors_config_t &monitors_conf)
{
    j.at("bitrate_monitor").get_to(monitors_conf.bitrate_monitor);
    j.at("cycle_monitor").get_to(monitors_conf.cycle_monitor);
}

void to_json(nlohmann::json &j, const hailo_encoder_config_t &enc_conf)
{
    j = nlohmann::json{{"encoding",
                        {{"input_stream", enc_conf.input_stream},
                         {"hailo_encoder",
                          {{"config", {{"output_stream", enc_conf.output_stream}}},
                           {"gop_config", enc_conf.gop},
                           {"coding_control", enc_conf.coding_control},
                           {"rate_control", enc_conf.rate_control},
                           {"monitors_control", enc_conf.monitors_control}}}}}};
}

void from_json(const nlohmann::json &j, hailo_encoder_config_t &enc_conf)
{
    j.at("encoding").at("input_stream").get_to(enc_conf.input_stream);
    j.at("encoding").at("hailo_encoder").at("config").at("output_stream").get_to(enc_conf.output_stream);
    j.at("encoding").at("hailo_encoder").at("gop_config").get_to(enc_conf.gop);
    j.at("encoding").at("hailo_encoder").at("coding_control").get_to(enc_conf.coding_control);
    j.at("encoding").at("hailo_encoder").at("rate_control").get_to(enc_conf.rate_control);
    j.at("encoding").at("hailo_encoder").at("monitors_control").get_to(enc_conf.monitors_control);
}

void to_json(nlohmann::json &j, const jpeg_encoder_config_t &enc_conf)
{
    j = nlohmann::json{{"encoding",
                        {{"input_stream", enc_conf.input_stream},
                         {"jpeg_encoder", {{"n_threads", enc_conf.n_threads}, {"quality", enc_conf.quality}}}}}};
}

void from_json(const nlohmann::json &j, jpeg_encoder_config_t &enc_conf)
{
    j.at("encoding").at("input_stream").get_to(enc_conf.input_stream);
    j.at("encoding").at("jpeg_encoder").at("n_threads").get_to(enc_conf.n_threads);
    j.at("encoding").at("jpeg_encoder").at("quality").get_to(enc_conf.quality);
}

//------------------------ dis_config_t ------------------------

void to_json(nlohmann::json &j, const dis_config_t &dis)
{
    j = nlohmann::json{
        {"enabled", dis.enabled},
        {"minimun_coefficient_filter", dis.minimun_coefficient_filter},
        {"decrement_coefficient_threshold", dis.decrement_coefficient_threshold},
        {"increment_coefficient_threshold", dis.increment_coefficient_threshold},
        {"running_average_coefficient", dis.running_average_coefficient},
        {"std_multiplier", dis.std_multiplier},
        {"black_corners_correction_enabled", dis.black_corners_correction_enabled},
        {"black_corners_threshold", dis.black_corners_threshold},
        {"average_luminance_threshold", dis.average_luminance_threshold},
        {"camera_fov_factor", dis.camera_fov_factor},
        {"angular_dis", dis.angular_dis_config},
        {"debug", dis.debug},
    };
}

void from_json(const nlohmann::json &j, dis_config_t &dis)
{
    j.at("enabled").get_to(dis.enabled);
    j.at("minimun_coefficient_filter").get_to(dis.minimun_coefficient_filter);
    j.at("decrement_coefficient_threshold").get_to(dis.decrement_coefficient_threshold);
    j.at("increment_coefficient_threshold").get_to(dis.increment_coefficient_threshold);
    j.at("running_average_coefficient").get_to(dis.running_average_coefficient);
    j.at("std_multiplier").get_to(dis.std_multiplier);
    j.at("black_corners_correction_enabled").get_to(dis.black_corners_correction_enabled);
    j.at("black_corners_threshold").get_to(dis.black_corners_threshold);
    j.at("average_luminance_threshold").get_to(dis.average_luminance_threshold);
    j.at("camera_fov_factor").get_to(dis.camera_fov_factor);
    j.at("angular_dis").get_to(dis.angular_dis_config);
    j.at("debug").get_to(dis.debug);
}

//------------------------ optical_zoom_config_t ------------------------

void to_json(nlohmann::json &j, const optical_zoom_config_t &oz_conf)
{
    j = nlohmann::json{{"enabled", oz_conf.enabled},
                       {"magnification", oz_conf.magnification},
                       {"max_dewarping_magnification", oz_conf.max_dewarping_magnification},
                       {"max_zoom_level", oz_conf.max_zoom_level}};
}

void from_json(const nlohmann::json &j, optical_zoom_config_t &oz_conf)
{
    j.at("enabled").get_to(oz_conf.enabled);
    j.at("magnification").get_to(oz_conf.magnification);
    oz_conf.max_dewarping_magnification = j.value("max_dewarping_magnification", 100.0); // use 100 as default value
    oz_conf.max_zoom_level = j.value("max_zoom_level", 40.0);                            // use 40.0 as default value
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
    };
    if (out_res.pool_max_buffers != 0)
    {
        j["pool_max_buffers"] = out_res.pool_max_buffers;
    }
}

void from_json(const nlohmann::json &j, output_resolution_t &out_res)
{
    j.at("framerate").get_to(out_res.framerate);
    j.at("height").get_to(out_res.dimensions.destination_height);
    j.at("width").get_to(out_res.dimensions.destination_width);
    out_res.keep_aspect_ratio =
        j.value("keep_aspect_ratio", false);                   // not a mandatory property, if not set, default to false
    out_res.pool_max_buffers = j.value("pool_max_buffers", 0); // not a mandatory property for input video
    out_res.dimensions.perform_crop = false;
}

//------------------------ application_input_streams_config_t ------------------------

void to_json(nlohmann::json &j, const application_input_streams_config_t &out_conf)
{
    j = nlohmann::json{
        {"method", out_conf.interpolation_type},
        {"format", out_conf.format},
        {"resolutions", out_conf.resolutions},
        {"grayscale", out_conf.grayscale},
    };
}

void from_json(const nlohmann::json &j, application_input_streams_config_t &out_conf)
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
        {"input_video",
         {
             {"source", in_conf.video_device},
             {"resolution", in_conf.resolution},
         }},
    };
}

void from_json(const nlohmann::json &j, input_video_config_t &in_conf)
{
    const auto &j2 = j.at("input_video");
    j2.at("resolution").get_to(in_conf.resolution);
    j2.at("source").get_to(in_conf.video_device);
}

//------------------------ motion_detection_config_t ------------------------

void to_json(nlohmann::json &j, const motion_detection_config_t &md_conf)
{
    j = nlohmann::json{
        {"enabled", md_conf.enabled},
        {"resolution", md_conf.resolution},
        {"roi", md_conf.roi},
        {"sensitivity_level", md_conf.sensitivity_level},
        {"threshold", md_conf.threshold},
    };
}

void from_json(const nlohmann::json &j, motion_detection_config_t &md_conf)
{
    j.at("enabled").get_to(md_conf.enabled);
    j.at("resolution").get_to(md_conf.resolution);
    j.at("roi").get_to(md_conf.roi);
    j.at("sensitivity_level").get_to(md_conf.sensitivity_level);
    j.at("threshold").get_to(md_conf.threshold);
}

//------------------------ multi_resize_config_t ------------------------

void to_json(nlohmann::json &j, const multi_resize_config_t &mresize_conf)
{
    // Although multi_resize_config_t has an input_video_config member, it is
    // not to be set/changed from json. It is set by the application.
    j = nlohmann::json{
        {"application_input_streams", mresize_conf.application_input_streams_config},
        {"digital_zoom", mresize_conf.digital_zoom_config},
        {"rotation", mresize_conf.rotation_config},
        {"motion_detection", mresize_conf.motion_detection_config},
    };
}

void from_json(const nlohmann::json &j, multi_resize_config_t &mresize_conf)
{
    // Although multi_resize_config_t has an input_video_config member, it is
    // not to be set/changed from json. It is set by the application.
    j.at("application_input_streams").get_to(mresize_conf.application_input_streams_config);
    j.at("digital_zoom").get_to(mresize_conf.digital_zoom_config);
    j.at("rotation").get_to(mresize_conf.rotation_config);
    j.at("motion_detection").get_to(mresize_conf.motion_detection_config);
}

//------------------------ eis_config_t ------------------------
void to_json(nlohmann::json &j, const eis_config_t &eis_conf)
{
    j = nlohmann::json{{"enabled", eis_conf.enabled},
                       {"stabilize", eis_conf.stabilize},
                       {"eis_config_path", eis_conf.eis_config_path},
                       {"window_size", eis_conf.window_size},
                       {"rotational_smoothing_coefficient", eis_conf.rotational_smoothing_coefficient},
                       {"iir_hpf_coefficient", eis_conf.iir_hpf_coefficient},
                       {"camera_fov_factor", eis_conf.camera_fov_factor},
                       {"line_readout_time", eis_conf.line_readout_time},
                       {"hdr_exposure_ratio", eis_conf.hdr_exposure_ratio},
                       {"min_angle_deg", eis_conf.min_angle_deg},
                       {"max_angle_deg", eis_conf.max_angle_deg},
                       {"shakes_type_buff_size", eis_conf.shakes_type_buff_size},
                       {"max_extensions_per_thr", eis_conf.max_extensions_per_thr},
                       {"min_extensions_per_thr", eis_conf.min_extensions_per_thr}};
}

void from_json(const nlohmann::json &j, eis_config_t &eis_conf)
{
    j.at("enabled").get_to(eis_conf.enabled);
    j.at("stabilize").get_to(eis_conf.stabilize);
    j.at("eis_config_path").get_to(eis_conf.eis_config_path);
    j.at("window_size").get_to(eis_conf.window_size);
    j.at("rotational_smoothing_coefficient").get_to(eis_conf.rotational_smoothing_coefficient);
    j.at("iir_hpf_coefficient").get_to(eis_conf.iir_hpf_coefficient);
    j.at("camera_fov_factor").get_to(eis_conf.camera_fov_factor);
    j.at("line_readout_time").get_to(eis_conf.line_readout_time);
    j.at("hdr_exposure_ratio").get_to(eis_conf.hdr_exposure_ratio);
    j.at("min_angle_deg").get_to(eis_conf.min_angle_deg);
    j.at("max_angle_deg").get_to(eis_conf.max_angle_deg);
    eis_conf.shakes_type_buff_size = j.value("shakes_type_buff_size", 300);  // use 300 as default value
    eis_conf.max_extensions_per_thr = j.value("max_extensions_per_thr", 30); // use 30 as default value
    eis_conf.min_extensions_per_thr = j.value("min_extensions_per_thr", 0);  // use 0 as default value
}

//------------------------ gyro_config_t ------------------------
void to_json(nlohmann::json &j, const gyro_config_t &gyro_conf)
{
    j = nlohmann::json{
        {"enabled", gyro_conf.enabled},
        {"sensor_name", gyro_conf.sensor_name},
        {"sensor_frequency", gyro_conf.sensor_frequency},
        {"scale", gyro_conf.gyro_scale},
    };
}

void from_json(const nlohmann::json &j, gyro_config_t &gyro_conf)
{
    j.at("enabled").get_to(gyro_conf.enabled);
    j.at("sensor_name").get_to(gyro_conf.sensor_name);
    j.at("sensor_frequency").get_to(gyro_conf.sensor_frequency);
    j.at("scale").get_to(gyro_conf.gyro_scale);
}

//------------------------ ldc_config_t ------------------------

void to_json(nlohmann::json &j, const ldc_config_t &ldc_conf)
{
    // Although ldc_config_t has an application_input_streams_config member, it is
    // not to be set/changed from json. It is set by the application.
    j = nlohmann::json{
        {"dewarp", ldc_conf.dewarp_config},
        {"dis", ldc_conf.dis_config},
        {"eis", ldc_conf.eis_config},
        {"gyro", ldc_conf.gyro_config},
        {"optical_zoom", ldc_conf.optical_zoom_config},
        {"rotation", ldc_conf.rotation_config},
        {"flip", ldc_conf.flip_config},
    };
}

void from_json(const nlohmann::json &j, ldc_config_t &ldc_conf)
{
    // Although ldc_config_t has an application_input_streams_config member, it is
    // not to be set/changed from json. It is set by the application.
    j.at("dewarp").get_to(ldc_conf.dewarp_config);
    j.at("dis").get_to(ldc_conf.dis_config);
    j.at("eis").get_to(ldc_conf.eis_config);
    j.at("gyro").get_to(ldc_conf.gyro_config);
    j.at("optical_zoom").get_to(ldc_conf.optical_zoom_config);
    j.at("rotation").get_to(ldc_conf.rotation_config);
    j.at("flip").get_to(ldc_conf.flip_config);

    ldc_conf.eis_config.num_exposures = 1;
    if (j.count("hdr") == 0)
        return;
    if (!j.at("hdr").at("enabled").get<bool>())
        return;
    ldc_conf.eis_config.num_exposures = j.at("hdr").at("dol").get<uint8_t>();
}

//------------------------ isp_t ------------------------

void to_json(nlohmann::json &j, const isp_t &isp_conf)
{
    j = nlohmann::json{
        {"isp",
         {
             {"auto-configuration", isp_conf.auto_configuration},
             {"isp_config_files_path", isp_conf.isp_config_files_path},
         }},
    };
}

void from_json(const nlohmann::json &j, isp_t &isp_conf)
{
    const auto &isp = j.at("isp");
    isp.at("auto-configuration").get_to(isp_conf.auto_configuration);
    isp.at("isp_config_files_path").get_to(isp_conf.isp_config_files_path);
}

//------------------------ hailort_t ------------------------

void to_json(nlohmann::json &j, const hailort_t &hrt_conf)
{
    j = nlohmann::json{
        {"hailort",
         {
             {"device-id", hrt_conf.device_id},
         }},
    };
}

void from_json(const nlohmann::json &j, hailort_t &hrt_conf)
{
    const auto &hailort = j.at("hailort");
    hailort.at("device-id").get_to(hrt_conf.device_id);
}

//------------------------ feedback_network_config_t ------------------------

void to_json(nlohmann::json &j, const feedback_network_config_t &net_conf)
{
    j = nlohmann::json{
        {"network_path", net_conf.network_path},
        {"y_channel", net_conf.y_channel},
        {"uv_channel", net_conf.uv_channel},
        {"feedback_y_channel", net_conf.feedback_y_channel},
        {"feedback_uv_channel", net_conf.feedback_uv_channel},
        {"output_y_channel", net_conf.output_y_channel},
        {"output_uv_channel", net_conf.output_uv_channel},
    };
}

void from_json(const nlohmann::json &j, feedback_network_config_t &net_conf)
{
    j.at("network_path").get_to(net_conf.network_path);
    j.at("y_channel").get_to(net_conf.y_channel);
    j.at("uv_channel").get_to(net_conf.uv_channel);
    j.at("feedback_y_channel").get_to(net_conf.feedback_y_channel);
    j.at("feedback_uv_channel").get_to(net_conf.feedback_uv_channel);
    j.at("output_y_channel").get_to(net_conf.output_y_channel);
    j.at("output_uv_channel").get_to(net_conf.output_uv_channel);
}

//------------------------ network_config_t ------------------------

void to_json(nlohmann::json &j, const network_config_t &net_conf)
{
    j = nlohmann::json{
        {"network_path", net_conf.network_path},
        {"y_channel", net_conf.y_channel},
        {"uv_channel", net_conf.uv_channel},
        {"output_y_channel", net_conf.output_y_channel},
        {"output_uv_channel", net_conf.output_uv_channel},
    };
}

void from_json(const nlohmann::json &j, network_config_t &net_conf)
{
    j.at("network_path").get_to(net_conf.network_path);
    j.at("y_channel").get_to(net_conf.y_channel);
    j.at("uv_channel").get_to(net_conf.uv_channel);
    j.at("output_y_channel").get_to(net_conf.output_y_channel);
    j.at("output_uv_channel").get_to(net_conf.output_uv_channel);
}

//------------------------ bayer_network_config_t ------------------------

void to_json(nlohmann::json &j, const bayer_network_config_t &net_conf)
{
    j = nlohmann::json{
        {"network_path", net_conf.network_path},
        {"bayer_channel", net_conf.bayer_channel},
        {"feedback_bayer_channel", net_conf.feedback_bayer_channel},
        {"output_bayer_channel", net_conf.output_bayer_channel},
    };
}

void from_json(const nlohmann::json &j, bayer_network_config_t &net_conf)
{
    j.at("network_path").get_to(net_conf.network_path);
    j.at("bayer_channel").get_to(net_conf.bayer_channel);
    j.at("feedback_bayer_channel").get_to(net_conf.feedback_bayer_channel);
    j.at("output_bayer_channel").get_to(net_conf.output_bayer_channel);
}

//------------------------ denoise_config_t ------------------------

void to_json(nlohmann::json &j, const denoise_config_t &d_conf)
{
    nlohmann::json network_json;
    if (d_conf.bayer == true)
    {
        network_json = d_conf.bayer_network_config;
    }
    else
    {
        network_json = d_conf.network_config;
    }

    j = nlohmann::json{{"denoise",
                        {{"enabled", d_conf.enabled},
                         {"bayer", d_conf.bayer},
                         {"sensor", d_conf.sensor},
                         {"method", d_conf.denoising_quality},
                         {"loopback-count", d_conf.loopback_count},
                         {"network", network_json}}}};
}

void from_json(const nlohmann::json &j, denoise_config_t &d_conf)
{
    const auto &denoise = j.at("denoise");
    denoise.at("enabled").get_to(d_conf.enabled);
    denoise.at("sensor").get_to(d_conf.sensor);
    denoise.at("method").get_to(d_conf.denoising_quality);
    denoise.at("loopback-count").get_to(d_conf.loopback_count);
    d_conf.bayer = denoise.value("bayer", false);
    if (d_conf.bayer == true)
    {
        denoise.at("network").get_to(d_conf.bayer_network_config);
    }
    else
    {
        denoise.at("network").get_to(d_conf.network_config);
    }
}

//------------------------ vsm_config_t ------------------------

void to_json(nlohmann::json &j, const vsm_config_t &vsm_conf)
{
    j = nlohmann::json{
        {"vsm",
         {
             {"vsm_h_size", vsm_conf.vsm_h_size},
             {"vsm_h_offset", vsm_conf.vsm_h_offset},
             {"vsm_v_size", vsm_conf.vsm_v_size},
             {"vsm_v_offset", vsm_conf.vsm_v_offset},
         }},
    };
}

void from_json(const nlohmann::json &j, vsm_config_t &vsm_conf)
{
    const auto &vsm = j.at("vsm");
    vsm.at("vsm_h_size").get_to(vsm_conf.vsm_h_size);
    vsm.at("vsm_h_offset").get_to(vsm_conf.vsm_h_offset);
    vsm.at("vsm_v_size").get_to(vsm_conf.vsm_v_size);
    vsm.at("vsm_v_offset").get_to(vsm_conf.vsm_v_offset);
}

//------------------------ hdr_config_t ------------------------

void to_json(nlohmann::json &j, const hdr_config_t &hdr_conf)
{
    j = nlohmann::json{
        {"hdr",
         {
             {"enabled", hdr_conf.enabled},
             {"dol", hdr_conf.dol},
             {"lsRatio", hdr_conf.ls_ratio},
             {"vsRatio", hdr_conf.vs_ratio},
         }},
    };
}

void from_json(const nlohmann::json &j, hdr_config_t &hdr_conf)
{
    const auto &hdr = j.at("hdr");
    hdr.at("enabled").get_to(hdr_conf.enabled);
    hdr.at("dol").get_to(hdr_conf.dol);
    // these ratios values only true for 2 DOL. Its hard codded because we curremtly support only 2 DOL
    hdr_conf.ls_ratio = hdr.value("lsRatio", 16); // 1048576/(1<<16 = 65536) = 16
    hdr_conf.vs_ratio = hdr.value("vsRatio", 4);  // 1048576/(1<<18 = 262144) = 4
}

//------------------------ isp_config_files_t ------------------------

void to_json(nlohmann::json &j, const isp_config_files_t &isp_conf)
{
    j = nlohmann::json{
        {"isp_config_files",
         {
             {"3a_config_path", isp_conf.aaa_config_path},
             {"sensor_entry", isp_conf.sensor_entry_path},
         }},
    };
}

void from_json(const nlohmann::json &j, isp_config_files_t &isp_conf)
{
    const auto &isp = j.at("isp_config_files");
    isp.at("3a_config_path").get_to(isp_conf.aaa_config_path);
    isp.at("sensor_entry").get_to(isp_conf.sensor_entry_path);
}

// Define serialization functions

void to_json(json &j, const OverrideParameters &p)
{
    j = json{{"override_file", p.override_file}, {"discard_on_profile_change", p.discard_on_profile_change}};
}

void from_json(const json &j, OverrideParameters &p)
{
    j.at("override_file").get_to(p.override_file);
    j.at("discard_on_profile_change").get_to(p.discard_on_profile_change);
}

void to_json(json &j, const profile_t &p)
{
    j = json{{"name", p.name}, {"config_file", p.config_file}};
}

void from_json(const json &j, profile_t &p)
{
    j.at("name").get_to(p.name);
    j.at("config_file").get_to(p.config_file);
}

void to_json(json &j, const medialib_config_t &m)
{
    j = json{{"default_profile", m.default_profile}, {"profiles", m.profiles}};
}

void from_json(const json &j, medialib_config_t &m)
{
    j.at("default_profile").get_to(m.default_profile);
    j.at("profiles").get_to(m.profiles);
}

//------------------------ codec_config_t ------------------------
void to_json(nlohmann::json &j, const codec_config_t &codec_conf)
{
    j = nlohmann::json{
        {"stream_id", codec_conf.stream_id},
        {"config_path", codec_conf.config_path},
    };
}

void from_json(const nlohmann::json &j, codec_config_t &codec_conf)
{
    j.at("stream_id").get_to(codec_conf.stream_id);
    j.at("config_path").get_to(codec_conf.config_path);
}

//------------------------ profile_config_t ------------------------

void to_json(json &j, const profile_config_t &profile_conf)
{
    j = json{
        {"application_input_streams", profile_conf.multi_resize_config},
        {"ldc", profile_conf.ldc_config},
        {"hailort", profile_conf.hailort_config},
        {"isp", profile_conf.isp_config},
        {"hdr", profile_conf.hdr_config},
        {"denoise", profile_conf.denoise_config},
        {"input_video", profile_conf.input_config},
        {"codec_configs", profile_conf.codec_configs},
        {"isp_config_files", profile_conf.isp_config_files},
    };
}

void from_json(const nlohmann::json &j, profile_config_t &profile_conf)
{
    j.get_to(profile_conf.multi_resize_config);
    j.get_to(profile_conf.ldc_config);
    j.get_to(profile_conf.hailort_config);
    j.get_to(profile_conf.isp_config);
    j.get_to(profile_conf.hdr_config);
    j.get_to(profile_conf.denoise_config);
    j.get_to(profile_conf.input_config);
    j.at("encoded_output_streams").get_to(profile_conf.codec_configs);
    j.get_to(profile_conf.isp_config_files);
}

// ------------------------ frontend_config_t --------------------------------

void to_json(json &j, const frontend_config_t &f_conf)
{
    j = json{};
    j.update(f_conf.input_config);
    j.update(f_conf.ldc_config);
    j.update(f_conf.denoise_config);
    j.update(f_conf.multi_resize_config);
    j.update(f_conf.hdr_config);
    j.update(f_conf.hailort_config);
    j.update(f_conf.isp_config);
}

void from_json(const json &j, frontend_config_t &f_conf)
{
    j.get_to(f_conf.input_config);
    j.get_to(f_conf.ldc_config);
    j.get_to(f_conf.denoise_config);
    j.get_to(f_conf.multi_resize_config);
    j.get_to(f_conf.hdr_config);
    j.get_to(f_conf.hailort_config);
    j.get_to(f_conf.isp_config);
}
#undef MODULE_NAME
