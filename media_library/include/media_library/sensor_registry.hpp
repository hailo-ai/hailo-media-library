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
 * @file sensor_registry.hpp
 * @brief MediaLibrary sensor type registry system
 **/

#pragma once

#include "sensor_types.hpp"

#include <optional>

class SensorRegistry
{
  public:
    static SensorRegistry &get_instance();
    std::optional<SensorCapabilities> get_sensor_capabilities(SensorType sensor) const;
    std::optional<SensorType> detect_sensor_type(size_t sensor_index = 0) const;
    std::optional<std::pair<int, std::string>> get_i2c_bus_and_address(size_t sensor_index);
    std::optional<Resolution> detect_resolution(const output_resolution_t &resolution) const;
    std::optional<ResolutionInfo> get_resolution_info(Resolution res) const;
    std::optional<SensorModeInfo> get_sensor_mode_info(SensorType sensor, const SensorModeKey &key) const;
    std::optional<SensorModeInfo> get_sensor_mode_info_hdr(const output_resolution_t &input_resolution,
                                                           const hdr_dol_t hdr_mode) const;
    std::optional<SensorModeInfo> get_sensor_mode_info_sdr(const output_resolution_t &input_resolution) const;
    bool is_supported(SensorCapabilities capabilities, Resolution resolution) const;
    std::optional<int> get_pixel_format();
    std::optional<std::string> get_video_device_path(size_t sensor_index);
    std::optional<std::string> get_raw_capture_path(size_t sensor_index);
    std::optional<std::string> get_sensor_name(SensorType sensor) const;
    std::optional<std::string> get_imx_subdevice_path(size_t sensor_index) const;

  private:
    struct SensorDeviceInfo
    {
        SensorType sensor_type;
        int bus;
        std::string address;
        std::string subdevice_path;
    };

    SensorRegistry();                                           // Private constructor for singleton
    SensorRegistry(const SensorRegistry &) = delete;            // No copy constructor
    SensorRegistry &operator=(const SensorRegistry &) = delete; // No copy assignment
    std::unordered_map<SensorType, SensorCapabilities> m_sensor_capabilities;
    std::unordered_map<Resolution, ResolutionInfo> m_resolution_info;

    void initialize_resolutions();
    void initialize_sensors();
    std::optional<SensorDeviceInfo> get_sensor_device_info(size_t sensor_index) const;
};
