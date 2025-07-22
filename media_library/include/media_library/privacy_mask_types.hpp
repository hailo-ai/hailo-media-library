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
 * @file privacy_mask_types.hpp
 * @brief MediaLibrary Privacy Mask type definitions
 **/
#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
#include "media_library_types.hpp"
#include "buffer_pool.hpp"

#define MAX_NUM_OF_STATIC_PRIVACY_MASKS 8
#define MAX_NUM_OF_DYNAMIC_PRIVACY_MASKS 100
#define MAX_NUM_OF_VERTICES_IN_POLYGON 8

/** @defgroup privacy_mask_types_definitions MediaLibrary Privacy Mask Types
 * API definitions
 *  @{
 */

namespace privacy_mask_types
{
struct yuv_color_t
{
    uint8_t y, u, v;
};

using PixelizationSize = size_t;

struct privacy_mask_info_t
{
    PrivacyMaskType type;
    union {
        yuv_color_t color;
        PixelizationSize pixelization_size; // Range: 2 to 64
    };
};

struct static_privacy_mask_data_t
{
    HailoMediaLibraryBufferPtr bitmask;
    roi_t rois[MAX_NUM_OF_STATIC_PRIVACY_MASKS];
    uint rois_count;

    static_privacy_mask_data_t() : bitmask(std::make_shared<hailo_media_library_buffer>()) {};
};
using StaticPrivacyMaskDataPtr = std::shared_ptr<static_privacy_mask_data_t>;

struct dynamic_privacy_mask_data_t
{
    dsp_dynamic_privacy_mask_t dynamic_mask_group;
};
using DynamicPrivacyMaskDataPtr = std::shared_ptr<dynamic_privacy_mask_data_t>;

struct privacy_masks_t
{
    StaticPrivacyMaskDataPtr static_data;
    DynamicPrivacyMaskDataPtr dynamic_data;
    privacy_mask_info_t info;

    privacy_masks_t()
    {
        static_data = std::make_shared<static_privacy_mask_data_t>();
        dynamic_data = std::make_shared<dynamic_privacy_mask_data_t>();
    };
    ~privacy_masks_t()
    {
        static_data = nullptr;
        dynamic_data = nullptr;
        info = {};
    };
};
using PrivacyMasksPtr = std::shared_ptr<privacy_masks_t>;
} // namespace privacy_mask_types

/** @} */ // end of privacy_mask_types_definitions
