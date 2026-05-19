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

#include <tl/expected.hpp>
#include <stddef.h>
#include <memory>
#include <future>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <utility>

#include "media_library/media_library_types.hpp"
#include "impl/custom_overlay_impl.hpp"
#include "impl/datetime_overlay_impl.hpp"
#include "impl/image_overlay_impl.hpp"
#include "impl/text_overlay_impl.hpp"
#include "impl/overlay_impl.hpp"
#include "osd_types.hpp"

using OverlayImplPtr = std::shared_ptr<OverlayImpl>;

namespace osd
{
class OverlayRepository : public std::enable_shared_from_this<OverlayRepository>
{
  public:
    static tl::expected<std::shared_ptr<OverlayRepository>, media_library_return> create();

    OverlayImplPtr get_overlay(const std::string &id);
    std::vector<OverlayImplPtr> get_blending_overlays(const config_stream_osd_t &config);

    tl::expected<OverlayImplPtr, media_library_return> upsert_overlay(const ImageOverlay &overlay);
    std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> upsert_overlay_async(
        const ImageOverlay &overlay);

    tl::expected<OverlayImplPtr, media_library_return> upsert_overlay(const TextOverlay &overlay);
    std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> upsert_overlay_async(
        const TextOverlay &overlay);

    tl::expected<OverlayImplPtr, media_library_return> upsert_overlay(const DateTimeOverlay &overlay);
    std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> upsert_overlay_async(
        const DateTimeOverlay &overlay);

    tl::expected<OverlayImplPtr, media_library_return> upsert_overlay(const CustomOverlay &overlay);
    std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> upsert_overlay_async(
        const CustomOverlay &overlay);

    media_library_return remove_overlay(const std::string &id);
    media_library_return set_frame_size(int frame_width, int frame_height);
    void update_overlays_if_needed(const config_stream_osd_t &config);

  private:
    class OverlayMap
    {
      public:
        std::unordered_map<std::string, ImageOverlayImplPtr> image;
        std::unordered_map<std::string, TextOverlayImplPtr> text;
        std::unordered_map<std::string, DateTimeOverlayImplPtr> datetime;
        std::unordered_map<std::string, CustomOverlayImplPtr> custom;

        OverlayImplPtr find(const std::string &id)
        {
            auto image_it = image.find(id);
            if (image_it != image.end())
                return image_it->second;
            auto text_it = text.find(id);
            if (text_it != text.end())
                return text_it->second;
            auto datetime_it = datetime.find(id);
            if (datetime_it != datetime.end())
                return datetime_it->second;
            auto custom_it = custom.find(id);
            if (custom_it != custom.end())
                return custom_it->second;
            return nullptr;
        }

        void erase(const std::string &id)
        {
            image.erase(id);
            text.erase(id);
            datetime.erase(id);
            custom.erase(id);
        }

        size_t size()
        {
            return image.size() + text.size() + datetime.size() + custom.size();
        }

        std::vector<std::string> filter_ids(std::function<bool(OverlayImplPtr)> filter, bool include_custom = false)
        {
            std::vector<std::string> ids;
            for (const auto &[id, overlay] : image)
            {
                if (filter(overlay))
                {
                    ids.push_back(id);
                }
            }
            for (const auto &[id, overlay] : text)
            {
                if (filter(overlay))
                {
                    ids.push_back(id);
                }
            }
            for (const auto &[id, overlay] : datetime)
            {
                if (filter(overlay))
                {
                    ids.push_back(id);
                }
            }

            if (!include_custom)
            {
                return ids;
            }

            for (const auto &[id, overlay] : custom)
            {
                if (filter(overlay))
                {
                    ids.push_back(id);
                }
            }
            return ids;
        }

        bool contains(const std::string &id)
        {
            return image.contains(id) || text.contains(id) || datetime.contains(id) || custom.contains(id);
        }
    };

    mutable std::shared_mutex m_mutex;
    int m_frame_width;
    int m_frame_height;
    bool m_frame_size_set;

    OverlayMap m_overlays;

    std::unordered_set<std::string> m_pending_removals;  // overlays that have been removed using the configurer, but
                                                         // there are still frames coming with the old configuration
    std::unordered_set<std::string> m_pending_additions; // overlays that are being added using the configurer, but
                                                         // there are still frames coming with the old configuration
};
} // namespace osd
