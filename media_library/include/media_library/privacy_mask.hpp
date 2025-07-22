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

#include <cstdint>
#include <sys/types.h>
#include <tl/expected.hpp>
#include <nlohmann/json.hpp>
#include <mutex>
#include "config_manager.hpp"
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
    /**
     * @brief Create a PrivacyMaskBlender object.
     *
     * @return tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> - An expected object that holds
     * either a shared pointer to a PrivacyMaskBlender object or an error code.
     */
    static tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> create();
    /**
     * @brief Create a PrivacyMaskBlender object using a configuration string.
     *
     * @param config - JSON configuration string.
     * @return tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> - An expected object that holds
     * either a shared pointer to a PrivacyMaskBlender object or an error code.
     */
    static tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> create(const std::string &config);
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
     * @brief Add a new static privacy mask
     *
     * @param privacy_mask - privacy mask to add
     * @return media_library_return - error code
     */
    media_library_return add_static_privacy_mask(const polygon &privacy_mask);

    /**
     * @brief Get static privacy mask
     *
     * @param id - privacy mask id
     * @return tl::expected<polygon, media_library_return> - polygon
     */
    tl::expected<polygon, media_library_return> get_static_privacy_mask(const std::string &id);

    /**
     * @brief Update existing static privacy mask
     *
     * @param privacy_mask - privacy mask to update
     * @return media_library_return - error code
     */
    media_library_return set_static_privacy_mask(const polygon &privacy_mask);

    /**
     * @brief Remove static privacy mask
     *
     * @param id - privacy mask id
     * @return media_library_return - error code
     */
    media_library_return remove_static_privacy_mask(const std::string &id);

    /**
     * @brief Change privacy mask to color mode and set color
     *
     * @param color - color to set
     * @return media_library_return - error code
     */
    media_library_return set_color(const rgb_color_t &color);

    /**
     * @brief Change privacy mask to pixelization mode and set pixelization size
     *
     * @param size - pixelization size affecting the intensity of the pixelization. Must be an even number between 2 and
     * 64
     * @return media_library_return - error code
     */
    media_library_return set_pixelization_size(PixelizationSize size);

    /**
     * @brief Set rotation
     *
     * @param rotation - rotation angle
     * @return media_library_return - error code
     */
    media_library_return set_rotation(const rotation_angle_t &rotation);

    /**
     * @brief Update all privacy masks.
     * Calculate the quantized bitmask representing all the static privacy masks combined, if enabled.
     * Get the latest dymanic privacy mask from the analytics database, if enabled.
     * Update the mask's info according to the current privacy mask type and settings.
     *
     * @return tl::expected<PrivacyMasksPtr, media_library_return> containing the bitmasks and relevant metadata.
     */
    tl::expected<PrivacyMasksPtr, media_library_return> get_updated_privacy_masks(uint64_t isp_timestamp_ns);

    /**
     * @brief Blend privacy masks
     * Update the current privacy masks, and blend them with the input buffer.
     *
     * @param input_buffer - input buffer to blend
     * @return media_library_return - error code
     */
    media_library_return blend(HailoMediaLibraryBufferPtr &input_buffer);

    /**
     * @brief Get color
     *
     * @return tl::expected<rgb_color_t, media_library_return> - the color if the privacy mask is in color mode, else
     * MEDIA_LIBRARY_ERROR
     */
    tl::expected<rgb_color_t, media_library_return> get_color();

    /**
     * @brief Get pixelization size
     *
     * @return tl::expected<PixelizationSize, media_library_return> - the pixelization size if the privacy mask is in
     * pixelization mode, else MEDIA_LIBRARY_ERROR
     */
    tl::expected<PixelizationSize, media_library_return> get_pixelization_size();

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
     * @brief Get all static privacy masks
     *
     * @return tl::expected<std::vector<polygon>, media_library_return> - vector of polygons
     */
    tl::expected<std::vector<polygon>, media_library_return> get_all_static_privacy_masks();

    /**
     * @brief Clear all static privacy masks
     *
     * @return media_library_return - error code
     */
    media_library_return clear_all_static_privacy_masks();

    /**
     * @brief Enable or disable static mask
     *
     * @param enable - true to enable static mask, false to disable
     */
    void set_static_mask_enabled(bool enable);

    /**
     * @brief Check if static mask is enabled
     *
     * @return bool - true if static mask is enabled, false otherwise
     */
    bool is_static_mask_enabled();

    /**
     * @brief Enable or disable dynamic mask
     *
     * @param enable - true to enable dynamic mask, false to disable
     */
    void set_dynamic_mask_enabled(bool enable);

    /**
     * @brief Check if dynamic mask is enabled
     *
     * @return bool - true if dynamic mask is enabled, false otherwise
     */
    bool is_dynamic_mask_enabled();

    /**
     * @brief Configure the PrivacyMaskBlender object using a JSON configuration string.
     *
     * @param config - JSON configuration string.
     * @return media_library_return - error code.
     */
    media_library_return configure(const std::string &config);

  private:
    std::vector<PolygonPtr> m_static_privacy_masks;

    PrivacyMaskType m_privacy_mask_type;
    std::optional<rgb_color_t> m_color;
    std::optional<PixelizationSize> m_pixelization_size; // Range: 2 to 64
    uint m_frame_width;
    uint m_frame_height;
    rotation_angle_t m_rotation;
    MediaLibraryBufferPoolPtr m_buffer_pool;
    std::mutex m_privacy_mask_mutex;
    PrivacyMasksPtr m_latest_privacy_masks;
    std::vector<dsp_dynamic_privacy_mask_roi_t> m_dynamic_masks_rois;

    std::string m_analytics_data_id;
    std::vector<std::string> m_masked_labels;
    size_t m_dilation_size;
    bool m_info_update_required;
    bool m_static_mask_update_required;
    bool m_static_mask_enabled;
    bool m_dynamic_mask_enabled;
    std::shared_ptr<ConfigManager> m_config_manager;
    media_library_return init_buffer_pool();
    media_library_return update_info();
    media_library_return update_static_mask();
    media_library_return update_dynamic_mask(uint64_t isp_timestamp_ns);
};
using PrivacyMaskBlenderPtr = std::shared_ptr<PrivacyMaskBlender>;

/** @} */ // end of privacy_mask_definitions
