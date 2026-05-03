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

#include "osd_repository.hpp"

#include "media_library/media_library_logger.hpp"
#include "osd_type_conversions.hpp"
#include "impl/custom_overlay_impl.hpp"
#include "impl/datetime_overlay_impl.hpp"
#include "impl/image_overlay_impl.hpp"
#include "impl/text_overlay_impl.hpp"

#include <algorithm>

#define MODULE_NAME LoggerType::Osd
namespace osd
{

template <typename TConfiguredOverlay, typename TOverlayMap, typename TMissingVector>
void process_configured_overlays(const std::vector<std::shared_ptr<TConfiguredOverlay>> &configured,
                                 const std::unordered_set<std::string> &pending_removals,
                                 const TOverlayMap &overlay_map, std::vector<OverlayImplPtr> &overlays,
                                 std::unordered_set<std::string> &requested_ids, TMissingVector &missing_overlays)
{
    for (const auto &overlay : configured)
    {
        if (pending_removals.contains(overlay->id))
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Skipping overlay id={} (pending removal)", overlay->id);
            continue;
        }

        auto it = overlay_map.find(overlay->id);
        if (it != overlay_map.end())
        {
            overlays.push_back(it->second);
            requested_ids.insert(overlay->id);
        }
        else
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Overlay id={} requested but missing in repository", overlay->id);
            missing_overlays.push_back(type_conversions::to_osd(*overlay));
        }
    }
}

tl::expected<std::shared_ptr<OverlayRepository>, media_library_return> OverlayRepository::create()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating OSD overlay repository");
    auto repo = std::make_shared<OverlayRepository>();
    repo->m_frame_width = 0;
    repo->m_frame_height = 0;
    repo->m_frame_size_set = false;
    return repo;
}

OverlayImplPtr OverlayRepository::get_overlay(const std::string &id)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Getting overlay id={}", id);
    std::shared_lock lock(m_mutex);
    OverlayImplPtr overlay = m_overlays.find(id);
    if (overlay == nullptr)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Overlay not found (id={})", id);
        return nullptr;
    }
    LOGGER__MODULE__TRACE(MODULE_NAME, "Overlay found (id={})", id);
    return overlay;
}

void OverlayRepository::update_overlays_if_needed(const config_stream_osd_t &config)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Updating overlays if needed");

    std::vector<osd::ImageOverlay> image_updates;
    std::vector<osd::TextOverlay> text_updates;
    std::vector<osd::DateTimeOverlay> datetime_updates;

    {
        std::shared_lock lock(m_mutex);

        for (const auto &configured_overlay : config.image_overlays)
        {
            if (!configured_overlay)
                continue;
            if (!m_overlays.image.contains(configured_overlay->id))
                continue;

            ImageOverlayImplPtr image_impl = m_overlays.image.at(configured_overlay->id);
            std::shared_ptr<osd::ImageOverlay> metadata =
                std::static_pointer_cast<osd::ImageOverlay>(image_impl->get_metadata());
            osd::ImageOverlay configured_osd = type_conversions::to_osd(*configured_overlay);
            if (*metadata != configured_osd)
            {
                image_updates.push_back(std::move(configured_osd));
            }
        }

        for (const auto &configured_overlay : config.datetime_overlays)
        {
            if (!configured_overlay)
                continue;
            if (!m_overlays.datetime.contains(configured_overlay->id))
                continue;

            DateTimeOverlayImplPtr datetime_impl = m_overlays.datetime.at(configured_overlay->id);
            std::shared_ptr<osd::DateTimeOverlay> metadata =
                std::static_pointer_cast<osd::DateTimeOverlay>(datetime_impl->get_metadata());
            osd::DateTimeOverlay configured_osd = type_conversions::to_osd(*configured_overlay);
            if (*metadata != configured_osd)
            {
                datetime_updates.push_back(std::move(configured_osd));
            }
        }

        for (const auto &configured_overlay : config.text_overlays)
        {
            if (!configured_overlay)
                continue;
            if (!m_overlays.text.contains(configured_overlay->id))
                continue;

            TextOverlayImplPtr text_impl = m_overlays.text.at(configured_overlay->id);
            std::shared_ptr<osd::TextOverlay> metadata =
                std::static_pointer_cast<osd::TextOverlay>(text_impl->get_metadata());
            osd::TextOverlay configured_osd = type_conversions::to_osd(*configured_overlay);
            if (*metadata != configured_osd)
            {
                text_updates.push_back(std::move(configured_osd));
            }
        }
    }

    for (const auto &overlay : image_updates)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Overlay id={} configuration changed, updating", overlay.id);
        (void)upsert_overlay(overlay);
    }
    for (const auto &overlay : datetime_updates)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Overlay id={} configuration changed, updating", overlay.id);
        (void)upsert_overlay(overlay);
    }
    for (const auto &overlay : text_updates)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Overlay id={} configuration changed, updating", overlay.id);
        (void)upsert_overlay(overlay);
    }
}

std::vector<OverlayImplPtr> OverlayRepository::get_blending_overlays(const config_stream_osd_t &config)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Computing blending overlays");
    std::vector<OverlayImplPtr> overlays;

    std::unordered_set<std::string> requested_ids;
    std::vector<std::string> pending_removals_snapshot;

    std::vector<ImageOverlay> missing_image_overlays;
    std::vector<TextOverlay> missing_text_overlays;
    std::vector<DateTimeOverlay> missing_datetime_overlays;

    std::vector<std::string> not_requested_anymore;

    {
        std::shared_lock lock(m_mutex);

        LOGGER__MODULE__TRACE(MODULE_NAME, "Repository state before scan: overlays={}, pending_removals={}",
                              m_overlays.size(), m_pending_removals.size());

        pending_removals_snapshot.insert(pending_removals_snapshot.end(), m_pending_removals.begin(),
                                         m_pending_removals.end());

        // Process configured overlays (config structs come from media_library_types.hpp)
        process_configured_overlays(config.image_overlays, m_pending_removals, m_overlays.image, overlays,
                                    requested_ids, missing_image_overlays);
        process_configured_overlays(config.text_overlays, m_pending_removals, m_overlays.text, overlays, requested_ids,
                                    missing_text_overlays);
        process_configured_overlays(config.datetime_overlays, m_pending_removals, m_overlays.datetime, overlays,
                                    requested_ids, missing_datetime_overlays);

        // in the case of Custom overlays, just add them all as they are not part of the configurer
        for (const auto &overlay : m_overlays.custom)
        {
            if (m_pending_removals.contains(overlay.first))
            {
                LOGGER__MODULE__TRACE(MODULE_NAME, "Skipping custom overlay id={} (pending removal)", overlay.first);
                continue;
            }
            if (!overlay.second->get_enabled())
            {
                LOGGER__MODULE__TRACE(MODULE_NAME, "Skipping custom overlay id={} (disabled)", overlay.first);
                continue;
            }

            overlays.push_back(overlay.second);
            requested_ids.insert(overlay.first);
        }

        not_requested_anymore = m_overlays.filter_ids(
            [&requested_ids, this](const OverlayImplPtr &overlay) {
                if (overlay == nullptr)
                {
                    return false;
                }

                const auto id = overlay->get_id();
                return !requested_ids.contains(id) && !m_pending_removals.contains(id);
            },
            false);
    }

    if (!not_requested_anymore.empty())
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Overlays not requested anymore (count={})", not_requested_anymore.size());
        // remove any overlays that are no longer requested
        std::unique_lock lock(m_mutex);
        for (const auto &id : not_requested_anymore)
        {
            m_overlays.erase(id);
            LOGGER__MODULE__INFO(MODULE_NAME, "Overlay id={} removed (not requested anymore)", id);
        }
    }

    // remove any overlays from m_overlays that are pending removal and have not been requested again
    {
        for (const auto &id : pending_removals_snapshot)
        {
            if (requested_ids.contains(id))
            {
                continue;
            }

            std::unique_lock lock(m_mutex);
            LOGGER__MODULE__INFO(MODULE_NAME, "Overlay id={} removed (pending removal)", id);
            m_overlays.erase(id);
            (void)m_pending_removals.erase(id);
        }
    }

    // add any overlays that have been requested but not found in m_overlays
    for (const auto &missing : missing_image_overlays)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating missing image overlay id={}", missing.id);
        tl::expected<OverlayImplPtr, media_library_return> val = upsert_overlay(missing);
        if (!val.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create missing overlay: {}", val.error());
            continue;
        }
    }
    for (const auto &missing : missing_text_overlays)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating missing text overlay id={}", missing.id);
        tl::expected<OverlayImplPtr, media_library_return> val = upsert_overlay(missing);
        if (!val.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create missing overlay: {}", val.error());
            continue;
        }
    }
    for (const auto &missing : missing_datetime_overlays)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating missing datetime overlay id={}", missing.id);
        tl::expected<OverlayImplPtr, media_library_return> val = upsert_overlay(missing);
        if (!val.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create missing overlay: {}", val.error());
            continue;
        }
    }

    LOGGER__MODULE__TRACE(MODULE_NAME, "Blending overlays resolved (count={}, missing_created={})", overlays.size(),
                          missing_datetime_overlays.size() + missing_image_overlays.size() +
                              missing_text_overlays.size());
    return overlays;
}

tl::expected<OverlayImplPtr, media_library_return> OverlayRepository::upsert_overlay(const ImageOverlay &overlay)
{
    {
        std::unique_lock lock(m_mutex);
        if (m_pending_additions.contains(overlay.id))
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Upsert custom overlay skipped (already pending) id={}", overlay.id);
            return tl::make_unexpected(MEDIA_LIBRARY_SUCCESS);
        }

        m_pending_additions.insert(overlay.id);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Upserting image overlay id={}", overlay.id);

    auto overlay_expected = ImageOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create image overlay {}", overlay.id);
        m_pending_additions.erase(overlay.id);
        return tl::make_unexpected(overlay_expected.error());
    }

    ImageOverlayImplPtr impl = overlay_expected.value();
    if (m_frame_size_set)
    {
        auto ret = impl->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure image overlay {}", overlay.id);
            m_pending_additions.erase(overlay.id);
            return tl::make_unexpected(ret.error());
        }
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Frame size not set, skipping dsp overlay creation for overlay id={}",
                             overlay.id);
    }
    std::unique_lock lock(m_mutex);
    m_overlays.image[overlay.id] = impl;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Image overlay upserted id={} (total={})", overlay.id, m_overlays.size());
    m_pending_additions.erase(overlay.id);
    return impl;
}

std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> OverlayRepository::upsert_overlay_async(
    const ImageOverlay &overlay)
{
    return std::async(std::launch::async, [this, overlay]() { return upsert_overlay(overlay); }).share();
}

tl::expected<OverlayImplPtr, media_library_return> OverlayRepository::upsert_overlay(const TextOverlay &overlay)
{
    {
        std::unique_lock lock(m_mutex);
        if (m_pending_additions.contains(overlay.id))
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Upsert custom overlay skipped (already pending) id={}", overlay.id);
            return tl::make_unexpected(MEDIA_LIBRARY_SUCCESS);
        }

        m_pending_additions.insert(overlay.id);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Upserting text overlay id={}", overlay.id);

    auto overlay_expected = TextOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create text overlay {}", overlay.id);
        m_pending_additions.erase(overlay.id);
        return tl::make_unexpected(overlay_expected.error());
    }

    TextOverlayImplPtr impl = overlay_expected.value();
    if (m_frame_size_set)
    {
        auto ret = impl->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure image overlay {}", overlay.id);
            m_pending_additions.erase(overlay.id);
            return tl::make_unexpected(ret.error());
        }
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Frame size not set, skipping dsp overlay creation for overlay id={}",
                             overlay.id);
    }
    std::unique_lock lock(m_mutex);
    m_overlays.text[overlay.id] = impl;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Text overlay upserted id={} (total={})", overlay.id, m_overlays.size());
    m_pending_additions.erase(overlay.id);
    return impl;
}

std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> OverlayRepository::upsert_overlay_async(
    const TextOverlay &overlay)
{
    return std::async(std::launch::async, [this, overlay]() { return upsert_overlay(overlay); }).share();
}

tl::expected<OverlayImplPtr, media_library_return> OverlayRepository::upsert_overlay(const DateTimeOverlay &overlay)
{
    {
        std::unique_lock lock(m_mutex);
        if (m_pending_additions.contains(overlay.id))
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Upsert custom overlay skipped (already pending) id={}", overlay.id);
            return tl::make_unexpected(MEDIA_LIBRARY_SUCCESS);
        }

        m_pending_additions.insert(overlay.id);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Upserting datetime overlay id={}", overlay.id);

    auto overlay_expected = DateTimeOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create datetime overlay {}", overlay.id);
        m_pending_additions.erase(overlay.id);
        return tl::make_unexpected(overlay_expected.error());
    }

    DateTimeOverlayImplPtr impl = overlay_expected.value();
    if (m_frame_size_set)
    {
        auto ret = impl->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure image overlay {}", overlay.id);
            m_pending_additions.erase(overlay.id);
            return tl::make_unexpected(ret.error());
        }
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Frame size not set, skipping dsp overlay creation for overlay id={}",
                             overlay.id);
    }

    std::unique_lock lock(m_mutex);
    m_overlays.datetime[overlay.id] = impl;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Datetime overlay upserted id={} (total={})", overlay.id, m_overlays.size());
    m_pending_additions.erase(overlay.id);
    return impl;
}

std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> OverlayRepository::upsert_overlay_async(
    const DateTimeOverlay &overlay)
{
    return std::async(std::launch::async, [this, overlay]() { return upsert_overlay(overlay); }).share();
}

tl::expected<OverlayImplPtr, media_library_return> OverlayRepository::upsert_overlay(const CustomOverlay &overlay)
{
    {
        std::unique_lock lock(m_mutex);
        if (m_pending_additions.contains(overlay.id))
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Upsert custom overlay skipped (already pending) id={}", overlay.id);
            return tl::make_unexpected(MEDIA_LIBRARY_SUCCESS);
        }

        m_pending_additions.insert(overlay.id);
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Upserting custom overlay id={}", overlay.id);

    auto overlay_expected = CustomOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create custom overlay {}", overlay.id);
        m_pending_additions.erase(overlay.id);
        return tl::make_unexpected(overlay_expected.error());
    }

    CustomOverlayImplPtr impl = overlay_expected.value();
    if (m_frame_size_set)
    {
        auto ret = impl->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure image overlay {}", overlay.id);
            m_pending_additions.erase(overlay.id);
            return tl::make_unexpected(ret.error());
        }
    }
    else
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Frame size not set, skipping dsp overlay creation for overlay id={}",
                             overlay.id);
    }

    std::unique_lock lock(m_mutex);
    m_overlays.custom[overlay.id] = impl;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Custom overlay upserted id={} (total={})", overlay.id, m_overlays.size());
    m_pending_additions.erase(overlay.id);
    return impl;
}

std::shared_future<tl::expected<OverlayImplPtr, media_library_return>> OverlayRepository::upsert_overlay_async(
    const CustomOverlay &overlay)
{
    return std::async(std::launch::async, [this, overlay]() { return upsert_overlay(overlay); }).share();
}

media_library_return OverlayRepository::remove_overlay(const std::string &id)
{
    std::unique_lock lock(m_mutex);
    if (!m_overlays.contains(id))
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Remove overlay no-op (not found) id={}", id);
        return MEDIA_LIBRARY_SUCCESS;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Overlay removal requested id={}", id);
    m_pending_removals.insert(id);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return OverlayRepository::set_frame_size(int frame_width, int frame_height)
{
    std::unique_lock lock(m_mutex);
    if (m_frame_width == frame_width && m_frame_height == frame_height)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Frame size unchanged ({}x{})", frame_width, frame_height);
        return MEDIA_LIBRARY_SUCCESS;
    }
    m_frame_size_set = true;
    m_frame_width = frame_width;
    m_frame_height = frame_height;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frame size set to {}x{}", frame_width, frame_height);

    // recalculate all overlays dsp data
    for (const auto &overlay_pair : m_overlays.image)
    {
        auto ret = overlay_pair.second->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to reconfigure image overlay {}", overlay_pair.first);
            return ret.error();
        }
    }
    for (const auto &overlay_pair : m_overlays.text)
    {
        auto ret = overlay_pair.second->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to reconfigure text overlay {}", overlay_pair.first);
            return ret.error();
        }
    }
    for (const auto &overlay_pair : m_overlays.datetime)
    {
        auto ret = overlay_pair.second->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to reconfigure datetime overlay {}", overlay_pair.first);
            return ret.error();
        }
    }
    for (const auto &overlay_pair : m_overlays.custom)
    {
        auto ret = overlay_pair.second->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to reconfigure custom overlay {}", overlay_pair.first);
            return ret.error();
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

} // namespace osd
