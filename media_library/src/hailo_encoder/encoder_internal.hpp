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
#include <iostream>
#include <memory>
#include <unordered_map>

extern "C"
{
#include "video_encoder/base_type.h"
#include "video_encoder/encinputlinebuffer.h"
#include "video_encoder/ewl.h"
#include "video_encoder/hevcencapi.h"
}

// Hailo includes
#include "buffer_pool.hpp"
#include "encoder_class.hpp"
#include "encoder_gop_config.hpp"
#include "encoder_internal.hpp"

struct EncoderCounters
{
  i32 picture_cnt;
  i32 picture_enc_cnt;
  u32 idr_interval;
  i32 last_idr_picture_cnt;
  u32 validencodedframenumber;
};

class Encoder::Impl final
{
private:
  const std::unordered_map<std::string, VCEncLevel> h265_level = {
      {"1.0", VCENC_HEVC_LEVEL_1}, {"2.0", VCENC_HEVC_LEVEL_2}, {"2.1", VCENC_HEVC_LEVEL_2_1}, {"3.0", VCENC_HEVC_LEVEL_3}, {"3.1", VCENC_HEVC_LEVEL_3_1}, {"4.0", VCENC_HEVC_LEVEL_4}, {"4.1", VCENC_HEVC_LEVEL_4_1}, {"5.0", VCENC_HEVC_LEVEL_5}, {"5.1", VCENC_HEVC_LEVEL_5_1}};
  const std::unordered_map<std::string, VCEncLevel> h264_level = {
      {"1.0", VCENC_H264_LEVEL_1}, {"1.1", VCENC_H264_LEVEL_1_1}, {"1.2", VCENC_H264_LEVEL_1_2}, {"1.3", VCENC_H264_LEVEL_1_3}, {"2.0", VCENC_H264_LEVEL_2}, {"2.1", VCENC_H264_LEVEL_2_1}, {"2.2", VCENC_H264_LEVEL_2_2}, {"3.0", VCENC_H264_LEVEL_3}, {"3.1", VCENC_H264_LEVEL_3_1}, {"3.2", VCENC_H264_LEVEL_3_2}, {"4.0", VCENC_H264_LEVEL_4}, {"4.1", VCENC_H264_LEVEL_4_1}, {"4.2", VCENC_H264_LEVEL_4_2}, {"5.0", VCENC_H264_LEVEL_5}, {"5.1", VCENC_H264_LEVEL_5_1}};
  const std::unordered_map<std::string, VCEncPictureType> input_formats = {
      {"I420", VCENC_YUV420_PLANAR},
      {"NV12", VCENC_YUV420_SEMIPLANAR},
      {"NV21", VCENC_YUV420_SEMIPLANAR_VU}};
  VCEncApiVersion m_encoder_version;
  VCEncBuild m_encoder_build;
  VCEncConfig m_vc_cfg;
  VCEncCodingCtrl m_vc_coding_cfg;
  VCEncRateCtrl m_vc_rate_cfg;
  VCEncPreProcessingCfg m_vc_pre_proc_cfg;
  uint32_t m_input_stride;

  // const char * const m_json_schema = load_json_schema();
  VCEncInst m_inst;
  VCEncIn m_enc_in;
  VCEncOut m_enc_out;
  int m_next_gop_size;
  VCEncPictureCodingType m_next_coding_type;
  EncoderCounters m_counters;
  void *m_ewl;
  bool m_multislice_encoding;
  EWLLinearMem_t m_output_memory;
  std::vector<EncoderInputBuffer> m_inputs;
  std::shared_ptr<EncoderConfig> m_config;
  class gopConfig;
  std::unique_ptr<gopConfig> m_gop_cfg;
  MediaLibraryBufferPoolPtr m_buffer_pool;

public:
  Impl(std::string json_string);
  ~Impl();
  std::vector<EncoderOutputBuffer> handle_frame(EncoderInputBuffer buf);
  void force_keyframe();
  void update_stride(uint32_t stride);
  int get_gop_size();
  std::shared_ptr<EncoderConfig> get_config();
  EncoderOutputBuffer start();
  EncoderOutputBuffer stop();

  // static const char *json_schema const get_json_schema() const;
  // static const char * const load_json_schema() const;
private:
  void updateArea(nlohmann::json &area, VCEncPictureArea &vc_area);
  void init_gop_config();
  void init_buffer_pool();
  VCEncRet init_coding_control_config();
  VCEncRet init_rate_control_config();
  VCEncRet init_preprocessing_config();
  VCEncRet init_encoder_config();
  VCEncLevel get_level(std::string level, bool codecH264);
  VCEncPictureType get_input_format(std::string format);
  VCEncPictureCodingType find_next_pic();
  media_library_return update_input_buffer(EncoderInputBuffer &buf);
  media_library_return create_output_buffer(EncoderOutputBuffer &output_buf);
  int allocate_output_memory();
  media_library_return
  encode_frame(EncoderInputBuffer &buf,
               std::vector<EncoderOutputBuffer> &outputs);
  media_library_return
  encode_multiple_frames(std::vector<EncoderOutputBuffer> &outputs);
  uint32_t get_codec();
  VCEncProfile get_profile();
};

class Encoder::Impl::gopConfig
{
private:
  VCEncGopConfig *m_gop_cfg;
  VCEncGopPicConfig m_gop_pic_cfg[MAX_GOP_PIC_CONFIG_NUM];
  int m_gop_size;
  char *m_gop_cfg_name;
  uint8_t m_gop_cfg_offset[MAX_GOP_SIZE + 1];
  int m_b_frame_qp_delta;
  bool m_codec_h264;
  int init_config();
  int ReadGopConfig(std::vector<GopPicConfig> &config, int gopSize);
  int ParseGopConfigLine(GopPicConfig &pic_cfg, int gopSize);

public:
  gopConfig(VCEncGopConfig *gopConfig, int gopSize, int bFrameQpDelta,
            bool codecH264);
  int get_gop_size() const;
  ~gopConfig() = default;
  const VCEncGopPicConfig *get_gop_pic_cfg() const { return m_gop_pic_cfg; }
  const uint8_t *get_gop_cfg_offset() const { return m_gop_cfg_offset; }
};