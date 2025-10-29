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
 * @file encoder_config_types.hpp
 * @brief encoder configuration type definitions
 **/

#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <variant>
#include <unordered_map>

enum codec_t
{
    CODEC_TYPE_H264,
    CODEC_TYPE_HEVC
};

inline const std::unordered_map<std::string, codec_t> str_to_codec{{"AVC", CODEC_TYPE_H264}, {"HEVC", CODEC_TYPE_HEVC}};

enum preset_mode_t
{
    GENERAL
};

inline const std::unordered_map<std::string, preset_mode_t> str_to_preset_mode{{"general", GENERAL}};

enum rc_mode_t
{
    VBR,
    CVBR,
    HRD,
    CQP
};

enum zoom_bitrate_adjuster_mode_t
{
    ZOOM_BITRATE_ADJUSTER_DISABLED,
    ZOOM_BITRATE_ADJUSTER_ZOOMING_PROCESS,
    ZOOM_BITRATE_ADJUSTER_ZOOM_LEVEL,
    ZOOM_BITRATE_ADJUSTER_BOTH
};

inline const std::unordered_map<std::string, rc_mode_t> str_to_rc_mode{
    {"VBR", VBR}, {"CVBR", CVBR}, {"HRD", HRD}, {"CQP", CQP}};

inline const std::unordered_map<rc_mode_t, std::string> rc_mode_to_str{
    {VBR, "VBR"}, {CVBR, "CVBR"}, {HRD, "HRD"}, {CQP, "CQP"}};

enum deblocking_filter_type_t
{
    DEBLOCKING_FILTER_ENABLED,
    DEBLOCKING_FILTER_DISABLED,
    DEBLOCKING_FILTER_DISABLED_ON_SLICE_EDGES
};

struct input_config_t
{
    uint32_t width;
    uint32_t height;
    uint32_t framerate;
    std::string format;
    uint32_t max_pool_size;

    bool operator==(const input_config_t &other) const
    {
        return width == other.width && height == other.height && framerate == other.framerate &&
               format == other.format && max_pool_size == other.max_pool_size;
    }
};

struct output_config_t
{
    codec_t codec;
    std::optional<std::string> profile;
    std::optional<std::string> level;

    bool operator==(const output_config_t &other) const
    {
        return codec == other.codec && profile == other.profile && level == other.level;
    }
};

struct deblocking_filter_t
{
    deblocking_filter_type_t type;
    int32_t tc_offset;
    int32_t beta_offset;
    bool deblock_override;

    bool operator==(const deblocking_filter_t &other) const
    {
        return type == other.type && tc_offset == other.tc_offset && beta_offset == other.beta_offset &&
               deblock_override == other.deblock_override;
    }
};

struct gop_config_t
{
    uint32_t gop_size;
    uint32_t b_frame_qp_delta;

    bool operator==(const gop_config_t &other) const
    {
        return gop_size == other.gop_size && b_frame_qp_delta == other.b_frame_qp_delta;
    }
};

struct coding_roi_t
{
    bool enable;
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;

    bool operator==(const coding_roi_t &other) const
    {
        return enable == other.enable && top == other.top && left == other.left && bottom == other.bottom &&
               right == other.right;
    }
};

struct coding_roi_area_t
{
    bool enable;
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
    uint32_t qp_delta;

    bool operator==(const coding_roi_area_t &other) const
    {
        return enable == other.enable && top == other.top && left == other.left && bottom == other.bottom &&
               right == other.right && qp_delta == other.qp_delta;
    }
};

struct coding_control_config_t
{
    bool sei_messages;
    deblocking_filter_t deblocking_filter;
    coding_roi_t intra_area;
    coding_roi_t ipcm_area1;
    coding_roi_t ipcm_area2;
    coding_roi_area_t roi_area1;
    coding_roi_area_t roi_area2;

    bool operator==(const coding_control_config_t &other) const
    {
        return sei_messages == other.sei_messages && deblocking_filter == other.deblocking_filter &&
               intra_area == other.intra_area && ipcm_area1 == other.ipcm_area1 && ipcm_area2 == other.ipcm_area2 &&
               roi_area1 == other.roi_area1 && roi_area2 == other.roi_area2;
    }
};

struct bitrate_config_t
{
    uint32_t target_bitrate;
    std::optional<uint32_t> bit_var_range_i;
    std::optional<uint32_t> bit_var_range_p;
    std::optional<uint32_t> bit_var_range_b;
    std::optional<uint32_t> tolerance_moving_bitrate;
    std::optional<uint32_t> variation;

    bool operator==(const bitrate_config_t &other) const
    {
        return target_bitrate == other.target_bitrate && bit_var_range_i == other.bit_var_range_i &&
               bit_var_range_p == other.bit_var_range_p && bit_var_range_b == other.bit_var_range_b &&
               tolerance_moving_bitrate == other.tolerance_moving_bitrate && variation == other.variation;
    }
};

struct quantization_config_t
{
    std::optional<uint32_t> qp_min;
    std::optional<uint32_t> qp_max;
    int32_t qp_hdr;
    std::optional<int32_t> intra_qp_delta;
    std::optional<uint32_t> fixed_intra_qp;

    bool operator==(const quantization_config_t &other) const
    {
        return qp_min == other.qp_min && qp_max == other.qp_max && qp_hdr == other.qp_hdr &&
               intra_qp_delta == other.intra_qp_delta && fixed_intra_qp == other.fixed_intra_qp;
    }
};

struct qp_smooth_settings_t
{
    std::optional<int32_t> qp_delta;             /* QP smooth QP delta parameter */
    std::optional<int32_t> qp_delta_limit;       /* QP smooth QP delta limit parameter */
    std::optional<uint32_t> qp_delta_step;       /* QP smooth QP delta increment parameter */
    std::optional<uint32_t> qp_delta_limit_step; /* QP smooth QP delta limit increment parameter */
    std::optional<float> alpha;                  /* QP smooth alpha parameter */
    std::optional<int32_t> q_step_divisor;       /* QP smooth Q step divisor parameter */

    bool operator==(const qp_smooth_settings_t &other) const
    {
        return qp_delta == other.qp_delta && qp_delta_limit == other.qp_delta_limit &&
               qp_delta_step == other.qp_delta_step && qp_delta_limit_step == other.qp_delta_limit_step &&
               alpha == other.alpha && q_step_divisor == other.q_step_divisor;
    }
};

struct gop_anomaly_bitrate_adjuster_t
{
    std::optional<bool> enable;                     /* Enable smooth bitrate control */
    std::optional<float> threshold_high;            /* High threshold for smooth bitrate */
    std::optional<float> threshold_low;             /* Low threshold for smooth bitrate */
    std::optional<float> max_target_bitrate_factor; /* Maximum target bitrate factor */
    std::optional<float> adjustment_factor;         /* Bitrate adjustment factor */

    bool operator==(const gop_anomaly_bitrate_adjuster_t &other) const
    {
        return enable == other.enable && threshold_high == other.threshold_high &&
               threshold_low == other.threshold_low && max_target_bitrate_factor == other.max_target_bitrate_factor &&
               adjustment_factor == other.adjustment_factor;
    }
};

struct zoom_bitrate_adjuster_t
{
    std::optional<zoom_bitrate_adjuster_mode_t> mode; // Disabled, Zooming process, Zoom level, Both
    std::optional<float> zooming_process_bitrate_factor;
    std::optional<uint32_t> zooming_process_timeout_ms;
    std::optional<uint32_t> zooming_process_max_bitrate;
    std::optional<bool> zooming_process_force_keyframe;
    std::optional<float> zoom_level_threshold;
    std::optional<float> zoom_level_bitrate_factor;

    bool operator==(const zoom_bitrate_adjuster_t &other) const
    {
        return mode == other.mode && zooming_process_bitrate_factor == other.zooming_process_bitrate_factor &&
               zooming_process_timeout_ms == other.zooming_process_timeout_ms &&
               zooming_process_max_bitrate == other.zooming_process_max_bitrate &&
               zooming_process_force_keyframe == other.zooming_process_force_keyframe &&
               zoom_level_threshold == other.zoom_level_threshold &&
               zoom_level_bitrate_factor == other.zoom_level_bitrate_factor;
    }
};

struct rate_control_config_t
{
    rc_mode_t rc_mode;
    bool picture_rc;
    bool picture_skip;
    std::optional<bool> ctb_rc;
    std::optional<bool> hrd;
    std::optional<bool> padding;
    std::optional<uint32_t> cvbr;
    std::optional<uint32_t> block_rc_size;
    std::optional<uint32_t> hrd_cpb_size;
    std::optional<uint32_t> monitor_frames;
    uint32_t intra_pic_rate;
    std::optional<uint32_t> gop_length;
    quantization_config_t quantization;
    bitrate_config_t bitrate;
    zoom_bitrate_adjuster_t zoom_bitrate_adjuster;
    qp_smooth_settings_t qp_smooth_settings;
    gop_anomaly_bitrate_adjuster_t gop_anomaly_bitrate_adjuster;

    bool operator==(const rate_control_config_t &other) const
    {
        return rc_mode == other.rc_mode && picture_rc == other.picture_rc && picture_skip == other.picture_skip &&
               ctb_rc == other.ctb_rc && hrd == other.hrd && padding == other.padding && cvbr == other.cvbr &&
               block_rc_size == other.block_rc_size && hrd_cpb_size == other.hrd_cpb_size &&
               monitor_frames == other.monitor_frames && intra_pic_rate == other.intra_pic_rate &&
               gop_length == other.gop_length && quantization == other.quantization && bitrate == other.bitrate &&
               zoom_bitrate_adjuster == other.zoom_bitrate_adjuster && qp_smooth_settings == other.qp_smooth_settings &&
               gop_anomaly_bitrate_adjuster == other.gop_anomaly_bitrate_adjuster;
    }
};

struct jpeg_encoder_config_t
{
    std::string config_path;
    input_config_t input_stream;
    uint32_t n_threads;
    uint32_t quality;

    bool operator==(const jpeg_encoder_config_t &other) const
    {
        return config_path == other.config_path && input_stream == other.input_stream && n_threads == other.n_threads &&
               quality == other.quality;
    }
};

struct bitrate_monitor_config_t
{
    bool enable;
    uint32_t period;
    std::string result_output_path;
    bool output_result_to_file;

    bool operator==(const bitrate_monitor_config_t &other) const
    {
        return enable == other.enable && period == other.period && result_output_path == other.result_output_path &&
               output_result_to_file == other.output_result_to_file;
    }
};

struct cycle_monitor_config_t
{
    bool enable;
    uint32_t start_delay;
    uint32_t deviation_threshold;
    std::string result_output_path;
    bool output_result_to_file;

    bool operator==(const cycle_monitor_config_t &other) const
    {
        return enable == other.enable && start_delay == other.start_delay &&
               deviation_threshold == other.deviation_threshold && result_output_path == other.result_output_path &&
               output_result_to_file == other.output_result_to_file;
    }
};

struct encoder_monitors_config_t
{
    bitrate_monitor_config_t bitrate_monitor;
    cycle_monitor_config_t cycle_monitor;

    bool operator==(const encoder_monitors_config_t &other) const
    {
        return bitrate_monitor == other.bitrate_monitor && cycle_monitor == other.cycle_monitor;
    }
};

struct hailo_encoder_config_t
{
    std::string config_path;
    input_config_t input_stream;
    output_config_t output_stream;
    gop_config_t gop;
    coding_control_config_t coding_control;
    rate_control_config_t rate_control;
    encoder_monitors_config_t monitors_control;

    bool operator==(const hailo_encoder_config_t &other) const
    {
        return config_path == other.config_path && input_stream == other.input_stream &&
               output_stream == other.output_stream && gop == other.gop && coding_control == other.coding_control &&
               rate_control == other.rate_control && monitors_control == other.monitors_control;
    }
};

typedef std::variant<jpeg_encoder_config_t, hailo_encoder_config_t> encoder_config_t;
