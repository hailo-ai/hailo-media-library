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
 * @file polygon_math.hpp
 * @brief MediaLibrary Polygon math CPP API module
 **/

#pragma once
#include "media_library_types.hpp"
#include "privacy_mask_types.hpp"

#define PRIVACY_MASK_QUANTIZATION (0.25)

/**
 * @brief Fills a privacy mask data structure with polygons.
 * 
 * The target is to represent the binary image as a vector of bytes (packaged_array),
 * Where each pixel represents 4 pixels in the original image,
 * and each byte in memory (uint8) contains 8 pixels
 * Then it is copied into void* bitmask, this way it can be send to the HailoDSP later.
 * 
 * We use the OpenCV fillPoly functionality to fill the bitmask using Scanline Fill Algorithm.
 * 
 * @param polygons Vector of polygons to fill.
 * @param frame_width The width of the frame.
 * @param frame_height The height of the frame.
 * @param color The color of the polygons (RGB).
 * @param privacy_mask_data The privacy mask data structure to fill.
 */
media_library_return write_polygons_to_privacy_mask_data(std::vector<privacy_mask_types::PolygonPtr> &polygons, const uint &frame_width, const uint &frame_height, const privacy_mask_types::rgb_color_t &color, privacy_mask_types::PrivacyMaskDataPtr privacy_mask_data);

/**
 * @brief Rotates a vector of polygons.
 * 
 * @param polygons Vector of polygons to rotate.
 * @param rotation_angle The rotation angle.
*/
media_library_return rotate_polygons(std::vector<privacy_mask_types::PolygonPtr> &polygons, double rotation_angle, uint frame_width, uint frame_height);

/**
 * @brief Rotates a polygon.
 * 
 * @param polygon The polygon to rotate.
 * @param rotation_angle The rotation angle.
*/
media_library_return rotate_polygon(privacy_mask_types::PolygonPtr polygon, double rotation_angle, uint frame_width, uint frame_height);
