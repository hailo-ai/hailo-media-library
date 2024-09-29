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
#include "encoder_config_presets.hpp"
#include "media_library_logger.hpp"
#include "csv.h"
#include <fstream>

#define COLUMN_COUNT 22

EncoderConfigPresets::EncoderConfigPresets()
{
    load_presets();
}

void EncoderConfigPresets::load_presets()
{
    io::CSVReader<COLUMN_COUNT> csv_reader(ENCODER_PRESET_FILE);
    csv_reader.read_header(io::ignore_extra_column, "preset", "codec", "width", "height", "bitrate", "rc_mode", "ctb_rc", "hrd",
        "profile", "level", "gop_length", "monitor_frames", "bit_var_range", "tolerance_moving_bitrate", "qp_min", "qp_max",
        "cvbr", "padding", "fixed_intra_qp", "intra_qp_delta", "hrd_cpb_size", "block_rc_size");

    encoder_preset_t curr_preset {};
    std::string rc_mode;
    std::string preset_mode;
    std::string codec;
    int8_t ctb_rc;
    int8_t hrd;
    while (csv_reader.read_row(preset_mode, codec, curr_preset.width, curr_preset.height, curr_preset.bitrate, rc_mode, ctb_rc,
        hrd, curr_preset.profile, curr_preset.level, curr_preset.gop_length, curr_preset.monitor_frames,
        curr_preset.bit_var_range, curr_preset.tolerance_moving_bitrate, curr_preset.qp_min, curr_preset.qp_max,
        curr_preset.cvbr, curr_preset.padding, curr_preset.fixed_intra_qp, curr_preset.intra_qp_delta, curr_preset.hrd_cpb_size,
        curr_preset.block_rc_size))
    {
        curr_preset.rc_mode = str_to_rc_mode.at(rc_mode);
        curr_preset.preset_mode = str_to_preset_mode.at(preset_mode);
        curr_preset.codec = str_to_codec.at(codec);
        curr_preset.ctb_rc = ctb_rc;
        curr_preset.hrd = hrd;
        m_presets.push_back(curr_preset);
        curr_preset = {};
    }

    // Sort the presets vector based on codec, preset, width, height, bitrate, and rc_mode
    std::sort(m_presets.begin(), m_presets.end(), [](const encoder_preset_t &a, const encoder_preset_t &b) {
        if (a.codec != b.codec) return a.codec < b.codec;
        if (a.preset_mode != b.preset_mode) return a.preset_mode < b.preset_mode;
        if (a.width != b.width) return a.width < b.width;
        if (a.height != b.height) return a.height < b.height;
        if (a.bitrate != b.bitrate) return a.bitrate < b.bitrate;
        return a.rc_mode <= b.rc_mode;
    });
}

tl::expected<encoder_preset_t, media_library_return> EncoderConfigPresets::get_preset(preset_mode_t preset_mode, codec_t codec,
    uint32_t width, uint32_t height, uint32_t bitrate, rc_mode_t rc_mode) const
{
    for (const auto &preset : m_presets)
    {
        if (preset_mode == preset.preset_mode && codec == preset.codec &&
            ((width <= preset.width && height <= preset.height) || (width <= preset.height && height <= preset.width)) && // Allow for width and height to be swapped
            bitrate <= preset.bitrate && rc_mode == preset.rc_mode)
        {
            return preset;
        }
    }

    LOGGER__ERROR("No preset found for preset_mode: {}, codec: {}, width: {}, height: {}, bitrate: {}, rc_mode: {}",
        preset_mode, codec, width, height, bitrate, rc_mode);
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR);
}

media_library_return EncoderConfigPresets::apply_preset(hailo_encoder_config_t &config) const
{
    auto preset = get_preset(GENERAL, config.output_stream.codec, config.input_stream.width, config.input_stream.height,
        config.rate_control.bitrate.target_bitrate, config.rate_control.rc_mode);
    if (!preset)
    {
        return preset.error();
    }

    if (!config.rate_control.ctb_rc.has_value()) { config.rate_control.ctb_rc = preset->ctb_rc; }
    if (!config.rate_control.hrd.has_value()) { config.rate_control.hrd = preset->hrd; }
    if (!config.output_stream.profile.has_value()) { config.output_stream.profile = preset->profile; }
    if (!config.output_stream.level.has_value()) { config.output_stream.level = preset->level; }
    if (!config.rate_control.gop_length.has_value()) { config.rate_control.gop_length = preset->gop_length; }
    if (!config.rate_control.monitor_frames.has_value()) { config.rate_control.monitor_frames = preset->monitor_frames; }
    if (!config.rate_control.quantization.qp_min.has_value()) { config.rate_control.quantization.qp_min = preset->qp_min; }
    if (!config.rate_control.quantization.qp_max.has_value()) { config.rate_control.quantization.qp_max = preset->qp_max; }
    if (!config.rate_control.cvbr.has_value()) { config.rate_control.cvbr = preset->cvbr; }
    if (!config.rate_control.block_rc_size.has_value()) { config.rate_control.block_rc_size = preset->block_rc_size; }

    if (!config.rate_control.quantization.fixed_intra_qp.has_value())
    {
        config.rate_control.quantization.fixed_intra_qp = preset->fixed_intra_qp;
    }

    if (!config.rate_control.quantization.intra_qp_delta.has_value())
    {
        config.rate_control.quantization.intra_qp_delta = preset->intra_qp_delta;
    }

    auto status = apply_padding(config, *preset);
    if (status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return status;
    }

    apply_hrd_cpb_size(config, *preset);
    apply_variation(config, *preset);

    auto preset_bit_var_range = get_preset_bit_var_range(config, *preset);
    apply_bit_var_range(config, *preset, preset_bit_var_range);
    status = apply_tolerance_moving_bitrate(config, *preset, preset_bit_var_range);
    if (status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return status;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return EncoderConfigPresets::apply_padding(hailo_encoder_config_t &config, const encoder_preset_t &preset) const
{
    if (!config.rate_control.padding.has_value())
    {
        if (preset.padding == USER_VALUE)
        {
            LOGGER__ERROR("Padding is set to 'user' in the preset, but no padding value is provided in the configuration");
            return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        config.rate_control.padding = std::stoi(preset.padding);
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

void EncoderConfigPresets::apply_hrd_cpb_size(hailo_encoder_config_t &config, const encoder_preset_t &preset) const
{
    if (!config.rate_control.hrd_cpb_size.has_value())
    {
        if (preset.hrd_cpb_size == AUTO_VALUE)
        {
            config.rate_control.hrd_cpb_size = config.rate_control.bitrate.target_bitrate;
        }
        else
        {
            config.rate_control.hrd_cpb_size = std::stoi(preset.hrd_cpb_size);
        }
    }
}

void EncoderConfigPresets::apply_variation(hailo_encoder_config_t &config, const encoder_preset_t &preset) const
{
    if (!config.rate_control.bitrate.variation.has_value())
    {
        if (preset.rc_mode == VBR)
        {
            config.rate_control.bitrate.variation = DEFAULT_VBR_VARIATION;
        }
        else if (preset.rc_mode == CVBR)
        {
            config.rate_control.bitrate.variation = DEFAULT_CVBR_VARIATION;
        }
    }
}

uint32_t EncoderConfigPresets::get_preset_bit_var_range(hailo_encoder_config_t &config, const encoder_preset_t &preset) const
{
    if (preset.bit_var_range == AUTO_VALUE)
    {
        return config.rate_control.bitrate.variation.value() - 5;
    }
    else
    {
        return std::stoi(preset.bit_var_range);
    }
}

void EncoderConfigPresets::apply_bit_var_range(hailo_encoder_config_t &config, const encoder_preset_t &preset,
    uint32_t preset_bit_var_range) const
{
    if (!config.rate_control.bitrate.bit_var_range_i.has_value())
    {
        config.rate_control.bitrate.bit_var_range_i = preset_bit_var_range;
    }

    if (!config.rate_control.bitrate.bit_var_range_p.has_value())
    {
        config.rate_control.bitrate.bit_var_range_p = preset_bit_var_range;
    }

    if (!config.rate_control.bitrate.bit_var_range_b.has_value())
    {
        config.rate_control.bitrate.bit_var_range_b = preset_bit_var_range;
    }
}

media_library_return EncoderConfigPresets::apply_tolerance_moving_bitrate(hailo_encoder_config_t &config,
    const encoder_preset_t &preset, uint32_t preset_bit_var_range) const
{
    if (!config.rate_control.bitrate.tolerance_moving_bitrate.has_value())
    {
        if (preset.tolerance_moving_bitrate == AUTO_VALUE)
        {
            if (preset.rc_mode == VBR)
            {
                config.rate_control.bitrate.tolerance_moving_bitrate = config.rate_control.bitrate.variation.value();
            }
            else if (preset.rc_mode == CVBR)
            {
                config.rate_control.bitrate.tolerance_moving_bitrate =
                    config.rate_control.bitrate.variation.value() - preset_bit_var_range;
            }
            else
            {
                LOGGER__ERROR("auto tolerance_moving_bitrate is only supported for VBR and CVBR");
                return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
        }
        else
        {
            preset_bit_var_range = std::stoi(preset.bit_var_range);
        }
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}