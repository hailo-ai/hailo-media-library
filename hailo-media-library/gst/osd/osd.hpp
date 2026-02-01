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
 * @file osd.hpp
 * @brief OSD (On Screen Display) CPP API module
 **/
#pragma once

#include "media_library/buffer_pool.hpp"
#include "media_library/dsp_utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/config_manager.hpp"
#include "osd_types.hpp"
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <tl/expected.hpp>

namespace osd
{
class OverlayRepository;
using OverlayRepositoryPtr = std::shared_ptr<OverlayRepository>;

/**
 * @}
 *
 * @brief Overlay manager
 * @details Support the addition, removal and modification of overlays
 *          Overlay traits are defined using the structs above.
 */

using Blender = class Configurer;
using BlenderPtr = std::shared_ptr<Blender>;

class Configurer : public std::enable_shared_from_this<Configurer>
{
  public:
    static tl::expected<std::shared_ptr<Configurer>, media_library_return> create(const std::string &stream_id,
                                                                                  const OverlayRepositoryPtr &repo);

    void set_stream_id(const std::string &stream_id);
    void set_config_manager_interactor(ConfigManagerInteractor *config_manager_interactor);

    /**
     * @brief Add a new overlay
     * @details The new overlay will be blended upon each subsequent call to :blend
     * @param[in] id Unique string identifier for the overlay.
     *               This id is used for all future operations on the overlay.
     * @param[in] overlay Overlay to add
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     */
    media_library_return add_overlay(const ImageOverlay &overlay);
    media_library_return add_overlay(const TextOverlay &overlay);
    media_library_return add_overlay(const DateTimeOverlay &overlay);
    media_library_return add_overlay(const CustomOverlay &overlay);
    media_library_return set_overlay_enabled(const std::string &id, bool enabled);

    std::shared_future<media_library_return> add_overlay_async(const ImageOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const TextOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const DateTimeOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const CustomOverlay &overlay);

    /**
     * @brief Retrieve info of an existing overlay.
     * @param[in] id String identifier of the overlay to get.
     * @return :shared_ptr holding the overlay info if successfull
     *         otherwise a :media_library_return error
     * @note The return overlay should be downcast to the correct overlay type using :std::static_pointer_cast
     *       The caller of the function is responsible to know which overlay type is denoted by each id
     *       Casting to an incorrect type might result in undefined behavior
     */
    tl::expected<std::shared_ptr<Overlay>, media_library_return> get_overlay(const std::string &id);

    /**
     * @brief Modify an existing overlay.
     * @param[in] id String identifier of the overlay to modify.
     * @param[in] overlay Overlay traits to modify
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     * @note All the traits in the :overlay param must be specified -
     *       that this fields which agree to remain unchanged must also be filled
     *       Use :get_overlay function to the current overlay state and perform changes
     */
    media_library_return set_overlay(const ImageOverlay &overlay);
    media_library_return set_overlay(const TextOverlay &overlay);
    media_library_return set_overlay(const DateTimeOverlay &overlay);
    media_library_return set_overlay(const CustomOverlay &overlay);

    std::shared_future<media_library_return> set_overlay_async(const ImageOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const TextOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const DateTimeOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const CustomOverlay &overlay);

    media_library_return configure(const std::string &config);
    /**
     * @brief Remove an exiting overlay
     * @details Subsequent calls to :blend will no longer blend the overlay
     * @param[in] id String identifier of the overlay to remove
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     */
    media_library_return remove_overlay(const std::string &id);
    std::shared_future<media_library_return> remove_overlay_async(const std::string &id);

  private:
    media_library_return add_overlay_to_profile(const ImageOverlay &overlay);
    media_library_return add_overlay_to_profile(const TextOverlay &overlay);
    media_library_return add_overlay_to_profile(const DateTimeOverlay &overlay);
    media_library_return add_overlay_to_profile(const CustomOverlay &overlay);
    media_library_return remove_overlay_from_profile(const std::string &id);
    media_library_return set_overlay_in_profile(const ImageOverlay &overlay);
    media_library_return set_overlay_in_profile(const TextOverlay &overlay);
    media_library_return set_overlay_in_profile(const DateTimeOverlay &overlay);
    media_library_return set_overlay_in_profile(const CustomOverlay &overlay);

    std::string m_stream_id;
    ConfigManagerInteractor *m_config_manager_interactor;
    OverlayRepositoryPtr m_osd_repository;
};

typedef std::shared_ptr<Configurer> ConfigurerPtr;

class OsdBlender
{
  public:
    OsdBlender();
    ~OsdBlender();
    static tl::expected<std::shared_ptr<OsdBlender>, media_library_return> create(const std::string &stream_id,
                                                                                  const OverlayRepositoryPtr &repo);

    void set_stream_id(const std::string &stream_id);

    media_library_return set_frame_size(int frame_width, int frame_height);
    media_library_return blend(HailoMediaLibraryBufferPtr &input_buffer);

  private:
    std::string m_stream_id;
    int m_frame_width;
    int m_frame_height;
    bool m_frame_size_set;
    OverlayRepositoryPtr m_osd_repository;
};

typedef std::shared_ptr<OsdBlender> OsdBlenderPtr;

} // namespace osd
