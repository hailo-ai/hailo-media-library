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
extern "C"
{
#include "video_encoder/hevcencapi.h"
}
#include "buffer_pool.hpp"
#include "encoder_config.hpp"
#include "media_library_types.hpp"

struct EncoderOutputBuffer
{
    HailoMediaLibraryBufferPtr buffer;
    uint32_t size;
    uint32_t frame_number;
    VCEncPictureCodingType frame_type;
    VCEncRet encoder_ret_code;
};

class Encoder
{
  public:
    Encoder(std::string json_string);
    ~Encoder();
    int get_gop_size();
    void force_keyframe();
    void update_stride(uint32_t stride);
    media_library_return configure(std::string json_string);
    media_library_return configure(const encoder_config_t &config);
    EncoderOutputBuffer get_encoder_header_output_buffer();
    encoder_config_t get_config();
    encoder_config_t get_user_config();
    std::vector<EncoderOutputBuffer> handle_frame(HailoMediaLibraryBufferPtr buf, uint32_t frame_number);
    tl::expected<EncoderOutputBuffer, media_library_return> start();
    tl::expected<EncoderOutputBuffer, media_library_return> finish();
    void stop();
    media_library_return init();
    media_library_return release();
    media_library_return dispose();
    encoder_monitors get_monitors();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
