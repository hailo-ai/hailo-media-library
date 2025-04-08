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
#include <variant>
#include <unordered_map>
#include <optional>

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
};

struct output_config_t
{
    codec_t codec;
    std::optional<std::string> profile;
    std::optional<std::string> level;
};

struct deblocking_filter_t
{
    deblocking_filter_type_t type;
    int32_t tc_offset;
    int32_t beta_offset;
    bool deblock_override;
};

struct gop_config_t
{
    uint32_t gop_size;
    uint32_t b_frame_qp_delta;
};

struct coding_roi_t
{
    bool enable;
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
};

struct coding_roi_area_t
{
    bool enable;
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
    uint32_t qp_delta;
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
};

struct bitrate_config_t
{
    uint32_t target_bitrate;
    std::optional<uint32_t> bit_var_range_i;
    std::optional<uint32_t> bit_var_range_p;
    std::optional<uint32_t> bit_var_range_b;
    std::optional<uint32_t> tolerance_moving_bitrate;
    std::optional<uint32_t> variation;
};

struct quantization_config_t
{
    std::optional<uint32_t> qp_min;
    std::optional<uint32_t> qp_max;
    int32_t qp_hdr;
    std::optional<int32_t> intra_qp_delta;
    std::optional<uint32_t> fixed_intra_qp;
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
};

struct jpeg_encoder_config_t
{
    std::string config_path;
    input_config_t input_stream;
    uint32_t n_threads;
    uint32_t quality;
};

struct bitrate_monitor_config_t
{
    bool enable;
    uint32_t period;
    std::string result_output_path;
    bool output_result_to_file;
};

struct cycle_monitor_config_t
{
    bool enable;
    uint32_t start_delay;
    uint32_t deviation_threshold;
    std::string result_output_path;
    bool output_result_to_file;
};

struct encoder_monitors_config_t
{
    bitrate_monitor_config_t bitrate_monitor;
    cycle_monitor_config_t cycle_monitor;
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
};

typedef std::variant<jpeg_encoder_config_t, hailo_encoder_config_t> encoder_config_t;
