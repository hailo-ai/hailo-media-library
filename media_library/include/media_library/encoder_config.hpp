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
#include <memory>
#include <vector>
#include <variant>

// Open source includes
#include "config_manager.hpp"
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

enum codec_t
{
    CODEC_TYPE_H264,
    CODEC_TYPE_HEVC
};

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
    std::string profile;
    std::string level;
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
    uint32_t bit_var_range_i;
    uint32_t bit_var_range_p;
    uint32_t bit_var_range_b;
    uint32_t tolerance_moving_bitrate;
};

struct quantization_config_t
{
    uint32_t qp_min;
    uint32_t qp_max;
    uint32_t qp_hdr;
    uint32_t intra_qp_delta;
    uint32_t fixed_intra_qp;
};

struct  rate_control_config_t
{
  bool picture_rc;
  bool picture_skip;
  bool ctb_rc;
  bool hrd;
  bool padding;
  uint32_t cvbr;
  uint32_t block_rc_size;
  uint32_t hrd_cpb_size;
  uint32_t monitor_frames;
  uint32_t intra_pic_rate;
  uint32_t gop_length;
  quantization_config_t quantization;
  bitrate_config_t bitrate;
};

struct jpeg_encoder_config_t
{
    input_config_t input_stream;
    uint32_t n_threads;
    uint32_t quality;
};

struct hailo_encoder_config_t
{
    input_config_t input_stream;
    output_config_t output_stream;
    gop_config_t gop;
    coding_control_config_t coding_control;
    rate_control_config_t rate_control;
};

typedef std::variant<jpeg_encoder_config_t, hailo_encoder_config_t> encoder_config_t;

class EncoderConfig
{
  private:
    EncoderType type;
    std::shared_ptr<ConfigManager> m_config_manager;
    std::string m_json_string;
    nlohmann::json m_doc;
    encoder_config_t m_config;
    struct input_config_t m_input_config;
    struct output_config_t m_output_config;
    // SchemaDocument m_schema_doc;
    // SchemaValidator m_validator;
  public:
    EncoderConfig(const std::string &config_path);
    const nlohmann::json &get_doc() const;
    media_library_return configure(const std::string &config_path);
    media_library_return configure(const encoder_config_t &encoder_config);
    encoder_config_t get_config();
    hailo_encoder_config_t get_hailo_config();
    jpeg_encoder_config_t get_jpeg_config();
};
