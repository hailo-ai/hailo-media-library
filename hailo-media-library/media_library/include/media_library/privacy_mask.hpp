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
#include "config_manager.hpp"
#include "media_library_types.hpp"
#include "privacy_mask_types.hpp"
#include "buffer_pool.hpp"
#include "analytics_db.hpp"

/** @defgroup privacy_mask_definitions MediaLibrary Privacy Mask CPP
 * API definitions
 *  @{
 */

using PrivacyMaskBlender = class PrivacyMaskConfigurer;
using PrivacyMaskBlenderPtr = std::shared_ptr<PrivacyMaskBlender>;
#define MASK_SIZE 128
using namespace privacy_mask_types;

class PrivacyMaskConfigurer : public std::enable_shared_from_this<PrivacyMaskConfigurer>
{
  public:
    /**
     * @brief Construct a new Privacy Mask Configurer object
     */
    PrivacyMaskConfigurer();

    /**
     * @brief Destructor for Privacy Mask Configurer object
     */
    ~PrivacyMaskConfigurer();

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
    media_library_return set_frame_size(const uint width, const uint height);

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
     * @return media_library_return - error code
     */
    media_library_return set_static_mask_enabled(bool enable);

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
    media_library_return set_dynamic_mask_enabled(bool enable);

    /**
     * @brief Check if dynamic mask is enabled
     *
     * @return bool - true if dynamic mask is enabled, false otherwise
     */
    bool is_dynamic_mask_enabled();

    /**
     * @brief Configure the PrivacyMaskConfigurer object using a JSON configuration string.
     *
     * @param config - JSON configuration string.
     * @return media_library_return - error code.
     */
    media_library_return configure(const std::string &config);

    /**
     * @brief Configure the PrivacyMaskConfigurer object using a privacy_mask_config_t structure.
     *
     * @param config - privacy_mask_config_t structure.
     * @return media_library_return - error code.
     */
    media_library_return configure(const std::unique_ptr<privacy_mask_config_t> &config);

    void set_stream_id(const std::string &stream_id);
    void set_config_manager_interactor(ConfigManagerInteractor *config_manager_interactor);

  private:
    std::string m_stream_id;
    ConfigManagerInteractor *m_config_manager_interactor = nullptr;
};

class PrivacyMask;
using PrivacyMaskPtr = std::shared_ptr<PrivacyMask>;

class PrivacyMask
{
    MediaLibraryBufferPoolPtr m_buffer_pool;
    std::string m_stream_id;
    PrivacyMasksPtr m_latest_privacy_masks;
    privacy_mask_config_t m_masking_config;
    std::vector<dsp_dynamic_privacy_mask_roi_t> m_dynamic_masks_rois;

    tl::expected<PrivacyMasksPtr, media_library_return> get_updated_privacy_masks(
        const config_encoded_output_stream_t &masking_config, uint64_t isp_timestamp_ns);
    size_t get_adjusted_frame_width(size_t width);
    size_t get_adjusted_frame_height(size_t height);
    bool should_adjust_buffer_pool(HailoMediaLibraryBufferPtr input_buffer);
    media_library_return adjust_buffer_pool(HailoMediaLibraryBufferPtr input_buffer);

    media_library_return update_info(const config_encoded_output_stream_t &masking_config);
    media_library_return update_static_mask(const config_encoded_output_stream_t &masking_config);
    media_library_return update_dynamic_mask(const config_encoded_output_stream_t &masking_config,
                                             uint64_t isp_timestamp_ns);
    // Helper methods for different analytics types
    media_library_return process_semantic_segmentation_masks(const AnalyticsQueryOptions &opts,
                                                             const application_analytics_config_t &analytics_config,
                                                             const std::string &analytics_data_id,
                                                             const std::vector<std::string> &masked_labels,
                                                             const size_t dilation_size);
    media_library_return process_detection_masks(const AnalyticsQueryOptions &opts,
                                                 const application_analytics_config_t &analytics_config,
                                                 const std::string &analytics_data_id,
                                                 const std::vector<std::string> &masked_labels,
                                                 const size_t dilation_size);

  public:
    PrivacyMask();
    ~PrivacyMask();
    static tl::expected<PrivacyMaskPtr, media_library_return> create();
    /**
     * @brief Blend privacy masks
     * Update the current privacy masks, and blend them with the input buffer.
     *
     * @param input_buffer - input buffer to blend
     * @return media_library_return - error code
     */
    media_library_return blend(HailoMediaLibraryBufferPtr input_buffer);
    void set_stream_id(const std::string &stream_id);
};

/** @} */ // end of privacy_mask_definitions
