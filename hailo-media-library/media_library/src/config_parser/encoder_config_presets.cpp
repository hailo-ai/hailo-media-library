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

#include <tl/expected.hpp>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#include "media_library_logger.hpp"
#include "csv.h"
#include "encoder_config_types.hpp"

#define MODULE_NAME LoggerType::Encoder

#define COLUMN_COUNT 10

EncoderConfigPresets::EncoderConfigPresets()
{
    load_presets();
}

void EncoderConfigPresets::load_presets()
{
    using CSVReader = io::CSVReader<COLUMN_COUNT, io::trim_chars<' ', '\t'>, io::no_quote_escape<','>,
                                    io::throw_on_overflow, io::single_and_empty_line_comment<'#'>>;
    CSVReader csv_reader(ENCODER_PRESET_FILE);
    csv_reader.read_header(io::ignore_extra_column, "rc_mode", "width", "height", "bitrate", "bit_var_range_i",
                           "tolerance_moving_bitrate", "qp_min", "qp_max", "fixed_intra_qp", "intra_qp_delta");

    encoder_preset_t curr_preset{};
    std::string rc_mode;
    while (csv_reader.read_row(rc_mode, curr_preset.width, curr_preset.height, curr_preset.bitrate,
                               curr_preset.bit_var_range_i, curr_preset.tolerance_moving_bitrate, curr_preset.qp_min,
                               curr_preset.qp_max, curr_preset.fixed_intra_qp, curr_preset.intra_qp_delta))
    {
        curr_preset.rc_mode = str_to_rc_mode.at(rc_mode);
        m_presets.push_back(curr_preset);
        curr_preset = {};
    }

    // Sort the presets vector based on rc_mode, width, height, and bitrate
    std::sort(m_presets.begin(), m_presets.end(), [](const encoder_preset_t &a, const encoder_preset_t &b) {
        if (a.rc_mode != b.rc_mode)
            return a.rc_mode < b.rc_mode;
        if (a.width != b.width)
            return a.width < b.width;
        if (a.height != b.height)
            return a.height < b.height;
        return a.bitrate < b.bitrate;
    });
}

tl::expected<encoder_preset_t, media_library_return> EncoderConfigPresets::get_preset(uint32_t width, uint32_t height,
                                                                                      uint32_t bitrate,
                                                                                      rc_mode_t rc_mode) const
{
    for (const auto &preset : m_presets)
    {
        if (preset.rc_mode == rc_mode &&
            ((width <= preset.width && height <= preset.height) ||
             (width <= preset.height && height <= preset.width)) && // Allow for width and height to be swapped
            bitrate <= preset.bitrate)
        {
            return preset;
        }
    }

    LOGGER__MODULE__ERROR(MODULE_NAME, "No preset found for rc_mode: {}, width: {}, height: {}, bitrate: {}",
                          rc_mode_to_str.at(rc_mode), width, height, bitrate);
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR);
}

media_library_return EncoderConfigPresets::apply_preset(hailo_encoder_config_t &config) const
{
    auto preset = get_preset(config.input_stream.width, config.input_stream.height,
                             config.rate_control.bitrate.target_bitrate, config.rate_control.rc_mode);
    if (!preset)
    {
        return preset.error();
    }

    // Apply bit_var_range_i (skip if "auto")
    if (!config.rate_control.bitrate.bit_var_range_i.has_value() && preset->bit_var_range_i != AUTO_VALUE)
    {
        try
        {
            config.rate_control.bitrate.bit_var_range_i = std::stoi(preset->bit_var_range_i);
        }
        catch (const std::invalid_argument &e)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid bit_var_range_i value: {}", preset->bit_var_range_i);
            return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // Apply tolerance_moving_bitrate (skip if "auto")
    if (!config.rate_control.bitrate.tolerance_moving_bitrate.has_value() &&
        preset->tolerance_moving_bitrate != AUTO_VALUE)
    {
        try
        {
            config.rate_control.bitrate.tolerance_moving_bitrate = std::stoi(preset->tolerance_moving_bitrate);
        }
        catch (const std::invalid_argument &e)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid tolerance_moving_bitrate value: {}",
                                  preset->tolerance_moving_bitrate);
            return media_library_return::MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // Apply qp_min
    if (!config.rate_control.quantization.qp_min.has_value())
    {
        config.rate_control.quantization.qp_min = preset->qp_min;
    }

    // Apply qp_max
    if (!config.rate_control.quantization.qp_max.has_value())
    {
        config.rate_control.quantization.qp_max = preset->qp_max;
    }

    // Apply fixed_intra_qp
    if (!config.rate_control.quantization.fixed_intra_qp.has_value())
    {
        config.rate_control.quantization.fixed_intra_qp = preset->fixed_intra_qp;
    }

    // Apply intra_qp_delta
    if (!config.rate_control.quantization.intra_qp_delta.has_value())
    {
        config.rate_control.quantization.intra_qp_delta = preset->intra_qp_delta;
    }

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}
