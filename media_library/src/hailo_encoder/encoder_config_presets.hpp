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

#include "encoder_config_types.hpp"
#include "media_library_types.hpp"
#include <cstdint>
#include <vector>
#include <tl/expected.hpp>

#define ENCODER_PRESET_FILE ("/etc/medialib/encoder_presets.csv")
#define DEFAULT_VBR_VARIATION (100)
#define DEFAULT_CVBR_VARIATION (15)
#define AUTO_VALUE ("auto")
#define USER_VALUE ("user")

struct encoder_preset_t
{
    // Preset Keys
    preset_mode_t preset_mode;
    codec_t codec;
    uint32_t width;
    uint32_t height;
    uint32_t bitrate;
    rc_mode_t rc_mode;

    bool ctb_rc;
    bool hrd;
    std::string profile;
    std::string level;
    uint32_t gop_length;
    uint32_t monitor_frames;
    std::string bit_var_range;
    std::string tolerance_moving_bitrate;
    uint32_t qp_min;
    uint32_t qp_max;
    uint32_t cvbr;
    std::string padding;
    uint32_t fixed_intra_qp;
    int32_t intra_qp_delta;
    std::string hrd_cpb_size;
    uint32_t block_rc_size;
};

class EncoderConfigPresets
{
  public:
    static EncoderConfigPresets &get_instance()
    {
        static EncoderConfigPresets instance;
        return instance;
    }

    tl::expected<encoder_preset_t, media_library_return> get_preset(preset_mode_t preset_mode, codec_t codec,
                                                                    uint32_t width, uint32_t height, uint32_t bitrate,
                                                                    rc_mode_t rc_mode) const;
    media_library_return apply_preset(hailo_encoder_config_t &config) const;

  private:
    EncoderConfigPresets();
    void load_presets();
    void apply_bit_var_range(hailo_encoder_config_t &config, uint32_t preset_bit_var_range) const;
    media_library_return apply_tolerance_moving_bitrate(hailo_encoder_config_t &config, const encoder_preset_t &preset,
                                                        uint32_t preset_tolerance_moving_bitrate) const;
    media_library_return apply_padding(hailo_encoder_config_t &config, const encoder_preset_t &preset) const;
    media_library_return apply_hrd_cpb_size(hailo_encoder_config_t &config, const encoder_preset_t &preset) const;
    media_library_return apply_variation(hailo_encoder_config_t &config, const encoder_preset_t &preset) const;
    tl::expected<uint32_t, media_library_return> get_preset_bit_var_range(hailo_encoder_config_t &config,
                                                                          const encoder_preset_t &preset) const;

    std::vector<encoder_preset_t> m_presets;
};
