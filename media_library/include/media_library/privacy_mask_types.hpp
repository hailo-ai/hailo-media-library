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

#define MAX_NUM_OF_PRIVACY_MASKS  8

/** @defgroup privacy_mask_types_definitions MediaLibrary Privacy Mask Types
 * API definitions
 *  @{
 */

namespace privacy_mask_types
{
  struct vertex
  {
      int x, y;

      vertex(int x, int y) : x(x), y(y) {}
  };

  struct rgb_color_t
  {
      uint r, g, b;
  };

  struct yuv_color_t
  {
      uint8_t y, u, v;
  };

  struct polygon
  {
      std::string id;
      std::vector<vertex> vertices;
  };
  using PolygonPtr = std::shared_ptr<polygon>;

  struct privacy_mask_data_t
  {
        HailoMediaLibraryBufferPtr bitmask;
        yuv_color_t color;
        roi_t rois[MAX_NUM_OF_PRIVACY_MASKS];
        uint rois_count;

        privacy_mask_data_t() : bitmask(std::make_shared<hailo_media_library_buffer>()) {};
  };
  using PrivacyMaskDataPtr = std::shared_ptr<privacy_mask_data_t>;
}

/** @} */ // end of privacy_mask_types_definitions