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
 * @file sensor_capabilities.hpp
 * @brief MediaLibrary sensor type registry system
 **/

#pragma once

#include "sensor_types.hpp"

#include "imx678_capabilities.hpp"
#include "imx675_capabilities.hpp"
#include "imx334_capabilities.hpp"
#include "imx715_capabilities.hpp"
#include "imx664_capabilities.hpp"

namespace sensor_config
{

inline const std::unordered_map<Resolution, ResolutionInfo> all_resolution_info{
    {Resolution::FHD, {.width = 1920, .height = 1080, .name = "fhd", .vsm_offsets = {.h_offset = 0, .v_offset = 0}}},

    {Resolution::UHD_4K,
     {.width = 3840, .height = 2160, .name = "4k", .vsm_offsets = {.h_offset = 960, .v_offset = 540}}},

    {Resolution::FIVE_MP,
     {.width = 2592, .height = 1944, .name = "5mp", .vsm_offsets = {.h_offset = 336, .v_offset = 432}}},

    {Resolution::FOUR_MP,
     {.width = 2688, .height = 1520, .name = "4mp", .vsm_offsets = {.h_offset = 384, .v_offset = 220}}},
};

inline std::unordered_map<SensorType, SensorCapabilities> all_sensor_capabilities{
    {SensorType::IMX678, sensor_config::imx678::capabilities},
    {SensorType::IMX675, sensor_config::imx675::capabilities},
    {SensorType::IMX334, sensor_config::imx334::capabilities},
    {SensorType::IMX715, sensor_config::imx715::capabilities},
    {SensorType::IMX664, sensor_config::imx664::capabilities},
};

inline std::vector<std::string> sensor_index_to_video_device{"/dev/video0", "/dev/video3"};
inline std::vector<std::string> sensor_index_to_raw_capture{"/dev/video2", "/dev/video5"};

} // namespace sensor_config
