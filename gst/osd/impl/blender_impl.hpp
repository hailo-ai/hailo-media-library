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

#pragma once
#include "media_library/config_manager.hpp"
#include "media_library/media_library_logger.hpp"
#include "media_library/media_library_types.hpp"
#include "overlay_impl.hpp"
#include "../osd.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <shared_mutex>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <opencv2/freetype.hpp>
#include <set>
#include <unordered_map>
#include <vector>
#include <functional>
#include <future>

namespace osd
{
class Blender::Impl final
{
  public:
    static tl::expected<std::unique_ptr<Blender::Impl>, media_library_return> create(const std::string &config);
    static std::shared_future<tl::expected<std::unique_ptr<Blender::Impl>, media_library_return>> create_async(
        const std::string &config);

    Impl(const nlohmann::json &config, media_library_return &status);
    ~Impl();
    media_library_return set_frame_size(int frame_width, int frame_height);

    media_library_return add_overlay(const ImageOverlay &overlay);
    media_library_return add_overlay(const TextOverlay &overlay);
    media_library_return add_overlay(const DateTimeOverlay &overlay);
    media_library_return add_overlay(const CustomOverlay &overlay);
    media_library_return set_overlay_enabled(const std::string &id, bool enabled);
    std::shared_future<media_library_return> add_overlay_async(const ImageOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const TextOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const DateTimeOverlay &overlay);

    media_library_return remove_overlay(const std::string &id);
    std::shared_future<media_library_return> remove_overlay_async(const std::string &id);

    tl::expected<std::shared_ptr<osd::Overlay>, media_library_return> get_overlay(const std::string &id);

    media_library_return set_overlay(const ImageOverlay &overlay);
    media_library_return set_overlay(const TextOverlay &overlay);
    media_library_return set_overlay(const DateTimeOverlay &overlay);
    media_library_return set_overlay(const CustomOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const ImageOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const TextOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const DateTimeOverlay &overlay);

    media_library_return blend(HailoMediaLibraryBufferPtr &input_buffer);
    media_library_return configure(const std::string &config);

  private:
    Impl(const std::string &config, media_library_return &status);
    media_library_return set_overlay(const OverlayImplPtr overlay);
    media_library_return add_overlay(const OverlayImplPtr overlay);
    media_library_return remove_overlay_internal(const std::string &id);
    media_library_return add_overlay_internal(const OverlayImplPtr overlay);

    void initialize_overlay_images();

    std::unordered_map<std::string, OverlayImplPtr> m_overlays;
    std::set<OverlayImplPtr> m_prioritized_overlays;

    std::shared_mutex m_mutex;

    nlohmann::json m_config;
    std::shared_ptr<ConfigManager> m_config_manager;

    int m_frame_width;
    int m_frame_height;
    bool m_frame_size_set;
};

} // namespace osd
