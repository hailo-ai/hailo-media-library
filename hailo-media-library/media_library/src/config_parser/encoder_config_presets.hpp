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
#define AUTO_VALUE ("auto")

struct encoder_preset_t
{
    // Preset Keys
    rc_mode_t rc_mode;
    uint32_t width;
    uint32_t height;
    uint32_t bitrate;

    // Preset Values
    std::string bit_var_range_i;
    std::string tolerance_moving_bitrate;
    uint32_t qp_min;
    uint32_t qp_max;
    uint32_t fixed_intra_qp;
    int32_t intra_qp_delta;
};

class EncoderConfigPresets
{
  public:
    static EncoderConfigPresets &get_instance()
    {
        static EncoderConfigPresets instance;
        return instance;
    }

    tl::expected<encoder_preset_t, media_library_return> get_preset(uint32_t width, uint32_t height, uint32_t bitrate,
                                                                    rc_mode_t rc_mode) const;
    media_library_return apply_preset(hailo_encoder_config_t &config) const;

  private:
    EncoderConfigPresets();
    void load_presets();

    std::vector<encoder_preset_t> m_presets;
};
