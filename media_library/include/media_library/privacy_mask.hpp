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
 * @file privacy_mask.hpp
 * @brief MediaLibrary Privacy Mask API module
 **/

#pragma once

#include <tl/expected.hpp>
#include <nlohmann/json.hpp>
#include <mutex>
#include "media_library_types.hpp"
#include "privacy_mask_types.hpp"
#include "buffer_pool.hpp"

/** @defgroup privacy_mask_definitions MediaLibrary Privacy Mask CPP
 * API definitions
 *  @{
 */

using namespace privacy_mask_types;

class PrivacyMaskBlender : public std::enable_shared_from_this<PrivacyMaskBlender>
{
  public:
    /**
     * @brief Construct a new Privacy Mask Blender object
     */
    PrivacyMaskBlender();

    /**
     * @brief Construct a new Privacy Mask Blender object
     *
     * @param frame_width - frame width
     * @param frame_height - frame height
     */
    PrivacyMaskBlender(uint frame_width, uint frame_height);

    /**
     * @brief Destructor for Privacy Mask Blender object
     */
    ~PrivacyMaskBlender();
    static tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> create();
    /**
     * @brief Create the PrivacyMaskBlender object
     *
     * @param[in] frame_width - frame width
     * @param[in] frame_height - frame height
     * @return tl::expected<PrivacyMaskBlenderPtr, media_library_return> -
     * An expected object that holds either a shared pointer
     * to an PrivacyMaskBlender object, or a error code.
     */
    static tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> create(uint frame_width,
                                                                                          uint frame_height);

    /**
     * @brief Add a new privacy mask
     *
     * @param privacy_mask - privacy mask to add
     * @return media_library_return - error code
     */
    media_library_return add_privacy_mask(const polygon &privacy_mask);

    /**
     * @brief Get privacy mask
     *
     * @param id - privacy mask id
     * @return tl::expected<polygon, media_library_return> - polygon
     */
    tl::expected<polygon, media_library_return> get_privacy_mask(const std::string &id);

    /**
     * @brief Update existing privacy mask
     *
     * @param privacy_mask - privacy mask to update
     * @return media_library_return - error code
     */
    media_library_return set_privacy_mask(const polygon &privacy_mask);

    /**
     * @brief Remove privacy mask
     *
     * @param id - privacy mask id
     * @return media_library_return - error code
     */
    media_library_return remove_privacy_mask(const std::string &id);

    /**
     * @brief Change privacy mask to color mode and set color
     *
     * @param color - color to set
     * @return media_library_return - error code
     */
    media_library_return set_color(const rgb_color_t &color);

    /**
     * @brief Change privacy mask to blur mode and set blur radius
     *
     * @param radius - blur radius affecting the intensity of the blur. Must be an even number between 2 and 64
     * @return media_library_return - error code
     */
    media_library_return set_blur_radius(size_t radius);

    /**
     * @brief Set rotation
     *
     * @param rotation - rotation angle
     * @return media_library_return - error code
     */
    media_library_return set_rotation(const rotation_angle_t &rotation);

    /**
     * @brief Blend privacy masks
     * calculate the quantized bitmask representing the all privacy masks combined
     *
     * @return tl::expected<PrivacyMaskDataPtr, media_library_return> containing the bitmask and relevant metadata.
     */
    tl::expected<PrivacyMaskDataPtr, media_library_return> blend();

    /**
     * @brief Get color
     *
     * @return tl::expected<rgb_color_t, media_library_return> - the color if the privacy mask is in color mode, else
     * MEDIA_LIBRARY_ERROR
     */
    tl::expected<rgb_color_t, media_library_return> get_color();

    /**
     * @brief Get blur radius
     *
     * @return tl::expected<size_t, media_library_return> - the blur radius if the privacy mask is in blur mode, else
     * MEDIA_LIBRARY_ERROR
     */
    tl::expected<size_t, media_library_return> get_blur_radius();

    /**
     * @brief Get frame size (width, height)
     *
     * @return tl::expected<std::pair<uint, uint>, media_library_return> - frame size width and height
     */
    tl::expected<std::pair<uint, uint>, media_library_return> get_frame_size();

    /**
     * @brief set_frame_size
     *
     * @param width - frame width
     * @param height - frame height
     * @return media_library_return - error code
     */
    media_library_return set_frame_size(const uint &width, const uint &height);

    /**
     * @brief Get all privacy masks
     *
     * @return tl::expected<std::vector<polygon>, media_library_return> - vector of polygons
     */
    tl::expected<std::vector<polygon>, media_library_return> get_all_privacy_masks();

  private:
    std::vector<PolygonPtr> m_privacy_masks;

    PrivacyMaskType m_privacy_mask_type;
    std::optional<rgb_color_t> m_color;
    std::optional<BlurRadius> m_blur_radius;
    uint m_frame_width;
    uint m_frame_height;
    rotation_angle_t m_rotation;
    MediaLibraryBufferPoolPtr m_buffer_pool;
    std::mutex m_privacy_mask_mutex;
    PrivacyMaskDataPtr m_latest_privacy_mask_data;
    bool m_update_required;
    media_library_return init_buffer_pool();
    void clean_latest_privacy_mask_data();
};
using PrivacyMaskBlenderPtr = std::shared_ptr<PrivacyMaskBlender>;

/** @} */ // end of privacy_mask_definitions
