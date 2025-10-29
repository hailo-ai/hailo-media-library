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
 * @file sensor_types.hpp
 * @brief MediaLibrary sensor and resolution definitions
 **/

#pragma once

#include "media_library_types.hpp"
#include <optional>
#include <set>
#include <string>

enum class SensorType
{
    IMX334,
    IMX664,
    IMX675,
    IMX678,
    IMX715
};

enum class Resolution
{
    FHD,     // 1920x1080
    UHD_4K,  // 3840x2160
    FIVE_MP, // 2592x1944
    FOUR_MP, // 2688x1520
};

enum CSI_MODE
{
    CSI_MODE_SDR = 0,
    CSI_MODE_MERCURY_ISP_STITCH_HDR = 1, // Only used for imx678 fhd hdr
    CSI_MODE_DEFAULT_HDR = 2,
};

struct ResolutionInfo
{
    uint32_t width;
    uint32_t height;
    std::string name;
    struct VSMOffsets
    {
        uint32_t h_offset;
        uint32_t v_offset;
    } vsm_offsets;
};

struct SensorModeInfo
{
    int sensor_mode;
    int csi_mode;
};

struct SensorModeKey
{
    Resolution resolution;
    std::optional<hdr_dol_t> hdr_mode;

    // Constructor for SDR modes
    SensorModeKey(Resolution res);

    // Constructor for all modes
    SensorModeKey(Resolution res, std::optional<hdr_dol_t> hdr);

    // Hash function for std::unordered_map
    struct Hash
    {
        std::size_t operator()(const SensorModeKey &key) const;
    };

    // Equality operator for std::unordered_map
    bool operator==(const SensorModeKey &other) const;
};

struct SensorCapabilities
{
    std::string sensor_name;
    std::string sub_dev_prefix;
    std::set<Resolution> supported_resolutions;
    int pixel_format;
    std::unordered_map<SensorModeKey, SensorModeInfo, SensorModeKey::Hash> mode_mappings;
};
