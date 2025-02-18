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
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <map>
#include <queue>
#include <ctime>
#include <fstream>
#include <tl/expected.hpp>

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

enum encoder_stream_restart_t
{
    STREAM_RESTART_NONE = 0,
    STREAM_RESTART,
    STREAM_RESTART_HARD
};

enum encoder_config_type_t
{
    ENCODER_CONFIG_RATE_CONTROL = 0,
    ENCODER_CONFIG_PRE_PROCESSING,
    ENCODER_CONFIG_CODING_CONTROL,
    ENCODER_CONFIG_GOP,
    ENCODER_CONFIG_STREAM,
    ENCODER_CONFIG_MONITORS
};

enum class encoder_operation_t
{
    ENCODER_OPERATION_START = 0,
    ENCODER_OPERATION_ENCODE,
    ENCODER_OPERATION_STOP
};
struct EncoderCounters
{
    i32 picture_cnt;
    i32 picture_enc_cnt;
    i32 last_idr_picture_cnt;
    u32 validencodedframenumber;
};

struct EncoderCycleMonitor
{
    bool enabled;
    u32 deviation_threshold;
    u32 monitor_frames;
    u32 start_delay;
    u32 frame_count;
    u32 sum;
    std::time_t start_time;
    std::ofstream output_file;
};

enum encoder_state_t
{
    ENCODER_STATE_UNINITIALIZED = 0,
    ENCODER_STATE_INITIALIZED,
    ENCODER_STATE_START,
    ENCODER_STATE_STOP
};

struct EncoderBitrateMonitor
{
    bool enabled;
    u32 fps;
    u32 period;
    u32 sum_period;
    u32 ma_bitrate;
    std::queue<u32> frame_sizes;
    std::ofstream output_file;
};

class Encoder::Impl final
{
  private:
    const std::map<std::string, VCEncLevel> h265_level = {
        {"1.0", VCENC_HEVC_LEVEL_1},   {"2.0", VCENC_HEVC_LEVEL_2},   {"2.1", VCENC_HEVC_LEVEL_2_1},
        {"3.0", VCENC_HEVC_LEVEL_3},   {"3.1", VCENC_HEVC_LEVEL_3_1}, {"4.0", VCENC_HEVC_LEVEL_4},
        {"4.1", VCENC_HEVC_LEVEL_4_1}, {"5.0", VCENC_HEVC_LEVEL_5},   {"5.1", VCENC_HEVC_LEVEL_5_1}};
    const std::map<std::string, VCEncLevel> h264_level = {
        {"1.0", VCENC_H264_LEVEL_1},   {"1.1", VCENC_H264_LEVEL_1_1}, {"1.2", VCENC_H264_LEVEL_1_2},
        {"1.3", VCENC_H264_LEVEL_1_3}, {"2.0", VCENC_H264_LEVEL_2},   {"2.1", VCENC_H264_LEVEL_2_1},
        {"2.2", VCENC_H264_LEVEL_2_2}, {"3.0", VCENC_H264_LEVEL_3},   {"3.1", VCENC_H264_LEVEL_3_1},
        {"3.2", VCENC_H264_LEVEL_3_2}, {"4.0", VCENC_H264_LEVEL_4},   {"4.1", VCENC_H264_LEVEL_4_1},
        {"4.2", VCENC_H264_LEVEL_4_2}, {"5.0", VCENC_H264_LEVEL_5},   {"5.1", VCENC_H264_LEVEL_5_1}};

    // Resolution to bitrate to level mapping
    const std::map<uint32_t, std::map<uint32_t, std::string>> h265_auto_level_map = {
        {720 * 480, {{UINT32_MAX, "3.0"}}},
        {960 * 540, {{2000000, "3.0"}, {UINT32_MAX, "3.1"}}},
        {1280 * 720, {{UINT32_MAX, "3.1"}}},
        {1920 * 1080, {{2000000, "3.1"}, {8000000, "4.0"}, {UINT32_MAX, "4.1"}}},
        {2048 * 1080, {{4000000, "4.0"}, {UINT32_MAX, "4.1"}}},
        {2560 * 1440, {{4000000, "5.0"}, {UINT32_MAX, "5.1"}}},
        {3840 * 2160, {{16000000, "5.0"}, {UINT32_MAX, "5.1"}}},
        {UINT32_MAX, {{25000000, "5.1"}, {UINT32_MAX, "5.1"}}}};
    const std::map<uint32_t, std::map<uint32_t, std::string>> h264_auto_level_map = {
        {720 * 480, {{UINT32_MAX, "3.0"}}},
        {1280 * 720, {{UINT32_MAX, "3.1"}}},
        {1920 * 1080, {{2000000, "3.1"}, {4000000, "3.2"}, {8000000, "4.0"}, {UINT32_MAX, "4.1"}}},
        {2560 * 1440, {{4000000, "4.0"}, {8000000, "4.1"}, {UINT32_MAX, "4.2"}}},
        {3840 * 2160, {{8000000, "4.2"}, {16000000, "5.0"}, {UINT32_MAX, "5.1"}}},
        {UINT32_MAX, {{25000000, "5.1"}, {UINT32_MAX, "5.2"}}}};

    const std::unordered_map<std::string, VCEncPictureType> input_formats = {
        {"I420", VCENC_YUV420_PLANAR}, {"NV12", VCENC_YUV420_SEMIPLANAR}, {"NV21", VCENC_YUV420_SEMIPLANAR_VU}};
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
    u32 m_intra_pic_rate;
    std::vector<std::pair<uint32_t, HailoMediaLibraryBufferPtr>> m_inputs;
    EncoderOutputBuffer m_header;
    std::shared_ptr<EncoderConfig> m_config;
    class gopConfig;
    std::unique_ptr<gopConfig> m_gop_cfg;
    MediaLibraryBufferPoolPtr m_buffer_pool;
    encoder_stream_restart_t m_stream_restart;
    encoder_state_t m_state;
    EncoderBitrateMonitor m_bitrate_monitor;
    EncoderCycleMonitor m_cycle_monitor;

    bool m_is_encoding_multiple_frames;
    std::mutex m_is_encoding_multiple_frames_mtx;
    std::condition_variable m_is_encoding_multiple_frames_cv;

    std::vector<encoder_config_type_t> m_update_required;

  public:
    Impl(std::string json_string);
    ~Impl();
    std::vector<EncoderOutputBuffer> handle_frame(HailoMediaLibraryBufferPtr buf, uint32_t frame_number);
    void force_keyframe();
    void update_stride(uint32_t stride);
    int get_gop_size();
    EncoderOutputBuffer get_encoder_header_output_buffer();
    media_library_return configure(std::string json_string);
    media_library_return configure(const encoder_config_t &config);
    encoder_config_t get_config();
    encoder_config_t get_user_config();
    tl::expected<EncoderOutputBuffer, media_library_return> start();
    void stop();
    tl::expected<EncoderOutputBuffer, media_library_return> finish();
    media_library_return init();
    media_library_return release();
    media_library_return dispose();
    encoder_monitors get_monitors();

    // static const char *json_schema const get_json_schema() const;
    // static const char * const load_json_schema() const;
  private:
    void updateArea(coding_roi_t &area, VCEncPictureArea &vc_area);
    void updateArea(coding_roi_area_t &area, VCEncPictureArea &vc_area);
    media_library_return init_gop_config();
    void create_gop_config();
    void init_buffer_pool(uint pool_size);
    media_library_return init_coding_control_config();
    media_library_return init_rate_control_config();
    media_library_return init_preprocessing_config();
    media_library_return init_encoder_config();
    media_library_return init_monitors_config();
    void stamp_time_and_log_fps(timespec &start_handle, timespec &end_handle);
    media_library_return get_level(std::string level, bool codecH264, u32 width, u32 height, u32 framerate,
                                   u32 framerate_denom, VCEncLevel &vc_level_out);
    media_library_return validate_level_limitations(std::string level, bool codecH264, u32 width, u32 height,
                                                    u32 framerate, u32 framerate_denom);
    media_library_return validate_bitrate_limitations(rate_control_config_t rate_control_config);
    VCEncPictureType get_input_format(std::string format);
    VCEncPictureCodingType find_next_pic();
    media_library_return update_input_buffer(HailoMediaLibraryBufferPtr buf);
    media_library_return allocate_output_memory(HailoMediaLibraryBufferPtr buffer_ptr);
    tl::expected<EncoderOutputBuffer, media_library_return> encode_executer(encoder_operation_t op);
    media_library_return update_configurations();
    media_library_return update_gop_configurations();
    media_library_return stream_restart();
    media_library_return encode_header();
    media_library_return encode_frame(HailoMediaLibraryBufferPtr buf, std::vector<EncoderOutputBuffer> &outputs,
                                      uint32_t frame_number);
    media_library_return encode_multiple_frames(std::vector<EncoderOutputBuffer> &outputs);
    uint32_t get_codec();
    bool hard_restart_required(const hailo_encoder_config_t &old_config, const hailo_encoder_config_t &new_config,
                               bool gop_update_required);
    bool gop_config_update_required(const hailo_encoder_config_t &old_config, const hailo_encoder_config_t &new_config);
    VCEncProfile get_profile(bool codecH264);
    void bitrate_monitor_sample();
    void cycle_monitor_sample();
    void monitor_write_to_file(std::ofstream &file, const std::string &data);
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
    int ReadGopConfig(std::vector<GopPicConfig> &config, int gopSize);
    int ParseGopConfigLine(GopPicConfig &pic_cfg);

  public:
    gopConfig(VCEncGopConfig *gopConfig, int gopSize, int bFrameQpDelta, bool codecH264);
    media_library_return init_config(VCEncGopConfig *gopConfig, int gop_size, int b_frame_qp_delta, bool codec_h264);
    int get_gop_size() const;
    ~gopConfig() = default;
    VCEncGopPicConfig *get_gop_pic_cfg()
    {
        return m_gop_pic_cfg;
    }
    const uint8_t *get_gop_cfg_offset() const
    {
        return m_gop_cfg_offset;
    }
};
