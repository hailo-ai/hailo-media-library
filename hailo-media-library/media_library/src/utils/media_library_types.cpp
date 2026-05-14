/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
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
 * @file media_library_types.cpp
 * @brief MediaLibrary type implementations
 **/

#include "media_library_types.hpp"

bool dis_config_t::operator==(const dis_config_t &other) const = default;

bool roi_t::operator==(const roi_t &other) const = default;
bool optical_zoom_config_t::operator==(const optical_zoom_config_t &other) const = default;
bool gyro_config_t::operator==(const gyro_config_t &other) const = default;
bool eis_config_t::operator==(const eis_config_t &other) const = default;
bool ldc_config_t::operator==(const ldc_config_t &other) const = default;
bool static_privacy_mask_config_t::operator==(const static_privacy_mask_config_t &other) const = default;
bool privacy_mask_config_t::operator==(const privacy_mask_config_t &other) const = default;
bool rgb_color_t::operator==(const rgb_color_t &other) const = default;
bool vertex::operator==(const vertex &other) const = default;
bool polygon::operator==(const polygon &other) const = default;
