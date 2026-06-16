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

#include <tl/expected.hpp>
#include <algorithm>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "media_library/media_library_logger.hpp"
#include "osd.hpp"
#include "osd_repository.hpp"
#include "osd_type_conversions.hpp"
#include "config_manager.hpp"
#include "config_parser.hpp"
#include "impl/overlay_impl.hpp"
#include "media_library_types.hpp"
#include "osd_types.hpp"

#define MODULE_NAME LoggerType::Osd

namespace osd
{
tl::expected<std::shared_ptr<Configurer>, media_library_return> Configurer::create(const std::string &stream_id,
                                                                                   const OverlayRepositoryPtr &repo)
{
    auto cfg = std::make_shared<Configurer>();
    cfg->m_stream_id = stream_id;
    cfg->m_config_manager_interactor = nullptr;

    if (repo == nullptr)
    {
        auto repo_expected = OverlayRepository::create();
        if (!repo_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create OSD repository ({})", repo_expected.error());
            return tl::make_unexpected(repo_expected.error());
        }
        cfg->m_osd_repository = repo_expected.value();
    }
    else
    {
        cfg->m_osd_repository = repo;
    }

    return cfg;
}

namespace
{
template <typename TOverlay>
media_library_return add_overlay_to_profile_impl(ConfigManagerInteractor *config_manager_interactor,
                                                 const std::string &stream_id, const TOverlay &overlay)
{
    auto current_profile_opt = config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    if (current_profile.encoded_output_streams.find(stream_id) == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", stream_id);
        return MEDIA_LIBRARY_ERROR;
    }

    auto &osd_config = current_profile.encoded_output_streams.at(stream_id).osd;

    if constexpr (std::is_same_v<std::decay_t<TOverlay>, ImageOverlay>)
    {
        auto media_overlay = osd::type_conversions::to_media(overlay);
        osd_config.image_overlays.push_back(std::make_shared<::ImageOverlay>(std::move(media_overlay)));
    }
    else if constexpr (std::is_same_v<std::decay_t<TOverlay>, TextOverlay>)
    {
        auto media_overlay = osd::type_conversions::to_media(overlay);
        osd_config.text_overlays.push_back(std::make_shared<::TextOverlay>(std::move(media_overlay)));
    }
    else if constexpr (std::is_same_v<std::decay_t<TOverlay>, DateTimeOverlay>)
    {
        auto media_overlay = osd::type_conversions::to_media(overlay);
        osd_config.datetime_overlays.push_back(std::make_shared<::DateTimeOverlay>(std::move(media_overlay)));
    }
    else if constexpr (std::is_same_v<std::decay_t<TOverlay>, CustomOverlay>)
    {
        return MEDIA_LIBRARY_SUCCESS; // Custom overlays cannot be added to profiles
    }

    return config_manager_interactor->set_profile(current_profile);
}

template <typename TOverlay>
media_library_return set_overlay_in_profile_impl(ConfigManagerInteractor *config_manager_interactor,
                                                 const std::string &stream_id, const TOverlay &overlay)
{
    auto current_profile_opt = config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    if (current_profile.encoded_output_streams.find(stream_id) == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", stream_id);
        return MEDIA_LIBRARY_ERROR;
    }

    auto &osd_config = current_profile.encoded_output_streams.at(stream_id).osd;
    if constexpr (std::is_same_v<std::decay_t<TOverlay>, ImageOverlay>)
    {
        auto media_overlay = osd::type_conversions::to_media(overlay);
        auto it = std::find_if(osd_config.image_overlays.begin(), osd_config.image_overlays.end(),
                               [&overlay](const auto &o) { return o && o->id == overlay.id; });
        if (it != osd_config.image_overlays.end())
        {
            if (*it)
            {
                **it = std::move(media_overlay);
            }
            else
            {
                *it = std::make_shared<::ImageOverlay>(std::move(media_overlay));
            }
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay id {} not found in profile", overlay.id);
            return MEDIA_LIBRARY_ERROR;
        }
    }
    else if constexpr (std::is_same_v<std::decay_t<TOverlay>, TextOverlay>)
    {
        auto media_overlay = osd::type_conversions::to_media(overlay);
        auto it = std::find_if(osd_config.text_overlays.begin(), osd_config.text_overlays.end(),
                               [&overlay](const auto &o) { return o && o->id == overlay.id; });
        if (it != osd_config.text_overlays.end())
        {
            if (*it)
            {
                **it = std::move(media_overlay);
            }
            else
            {
                *it = std::make_shared<::TextOverlay>(std::move(media_overlay));
            }
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay id {} not found in profile", overlay.id);
            return MEDIA_LIBRARY_ERROR;
        }
    }
    else if constexpr (std::is_same_v<std::decay_t<TOverlay>, DateTimeOverlay>)
    {
        auto media_overlay = osd::type_conversions::to_media(overlay);
        auto it = std::find_if(osd_config.datetime_overlays.begin(), osd_config.datetime_overlays.end(),
                               [&overlay](const auto &o) { return o && o->id == overlay.id; });
        if (it != osd_config.datetime_overlays.end())
        {
            if (*it)
            {
                **it = std::move(media_overlay);
            }
            else
            {
                *it = std::make_shared<::DateTimeOverlay>(std::move(media_overlay));
            }
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay id {} not found in profile", overlay.id);
            return MEDIA_LIBRARY_ERROR;
        }
    }
    else if constexpr (std::is_same_v<std::decay_t<TOverlay>, CustomOverlay>)
    {
        return MEDIA_LIBRARY_SUCCESS; // Custom overlays cannot be set in profiles
    }

    return config_manager_interactor->set_profile(current_profile);
}
} // namespace

void Configurer::set_stream_id(const std::string &stream_id)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting stream id to {}", stream_id);
    m_stream_id = stream_id;
}

void Configurer::set_config_manager_interactor(ConfigManagerInteractor *config_manager_interactor)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting config manager interactor");
    m_config_manager_interactor = config_manager_interactor;
}

media_library_return Configurer::configure(const std::string &config)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Configuring OSD with config");

    // check if the config has ' at the beginning and end of the string. if so, remove them
    std::string clean_config = config; // config is const, so we need to copy it
    if (clean_config[0] == '\'' && clean_config[clean_config.size() - 1] == '\'')
    {
        clean_config = clean_config.substr(1, config.size() - 2);
    }

    ConfigParser config_parser(CONFIG_SCHEMA_OSD);
    if (config_parser.validate_configuration(clean_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    config_stream_osd_t osd_config;
    auto ret = config_parser.config_string_to_struct(clean_config, osd_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse OSD configuration to struct");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    if (current_profile.encoded_output_streams.find(m_stream_id) == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    current_profile.encoded_output_streams.at(m_stream_id).osd = osd_config;
    return m_config_manager_interactor->set_profile(current_profile);
}

media_library_return Configurer::add_overlay_to_profile(const ImageOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Adding image overlay to profile id={}", overlay.id);
    return add_overlay_to_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::add_overlay_to_profile(const TextOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Adding text overlay to profile id={}", overlay.id);
    return add_overlay_to_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::add_overlay_to_profile(const DateTimeOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Adding datetime overlay to profile id={}", overlay.id);
    return add_overlay_to_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::add_overlay_to_profile(const CustomOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Adding custom overlay to profile id={}", overlay.id);
    return add_overlay_to_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::remove_overlay_from_profile(const std::string &id)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing overlay from profile id={}", id);
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    if (current_profile.encoded_output_streams.find(m_stream_id) == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }

    auto &osd_config = current_profile.encoded_output_streams.at(m_stream_id).osd;

    auto remove_overlay_by_id = [&](auto &overlays) {
        auto it = std::remove_if(overlays.begin(), overlays.end(), [&id](const auto &o) { return o && o->id == id; });
        if (it != overlays.end())
        {
            overlays.erase(it, overlays.end());
            return true;
        }
        return false;
    };
    if (remove_overlay_by_id(osd_config.image_overlays) || remove_overlay_by_id(osd_config.text_overlays) ||
        remove_overlay_by_id(osd_config.datetime_overlays))
    {
        return m_config_manager_interactor->set_profile(current_profile);
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Overlay id {} removed from profile", id);
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::set_overlay_in_profile(const ImageOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting image overlay in profile id={}", overlay.id);
    return set_overlay_in_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::set_overlay_in_profile(const TextOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting text overlay in profile id={}", overlay.id);
    return set_overlay_in_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::set_overlay_in_profile(const DateTimeOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting datetime overlay in profile id={}", overlay.id);
    return set_overlay_in_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

media_library_return Configurer::set_overlay_in_profile(const CustomOverlay &overlay)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting custom overlay in profile id={}", overlay.id);
    return set_overlay_in_profile_impl(m_config_manager_interactor, m_stream_id, overlay);
}

std::shared_future<media_library_return> Configurer::add_overlay_async(const DateTimeOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

std::shared_future<media_library_return> Configurer::add_overlay_async(const ImageOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

std::shared_future<media_library_return> Configurer::add_overlay_async(const TextOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

std::shared_future<media_library_return> Configurer::add_overlay_async(const CustomOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

media_library_return Configurer::add_overlay(const ImageOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::add_overlay(const TextOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::add_overlay(const DateTimeOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::add_overlay(const CustomOverlay &overlay)
{
    auto ret = add_overlay_to_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::set_overlay_enabled(const std::string &id, bool enabled)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Setting overlay enabled={} for id={}", enabled, id);
    auto overlay = m_osd_repository->get_overlay(id);
    if (overlay == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay id={} not found", id);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    overlay->set_enabled(enabled);
    return MEDIA_LIBRARY_SUCCESS;
}
media_library_return Configurer::remove_overlay(const std::string &id)
{
    auto ret = remove_overlay_from_profile(id);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    return m_osd_repository->remove_overlay(id);
}

std::shared_future<media_library_return> Configurer::remove_overlay_async(const std::string &id)
{
    return std::async(std::launch::async, [this, id]() { return remove_overlay(id); }).share();
}

tl::expected<std::shared_ptr<osd::Overlay>, media_library_return> Configurer::get_overlay(const std::string &id)
{
    auto overlay = m_osd_repository->get_overlay(id);
    if (overlay == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay id={} not found", id);
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    return overlay->get_metadata();
}

std::shared_future<media_library_return> Configurer::set_overlay_async(const ImageOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

std::shared_future<media_library_return> Configurer::set_overlay_async(const TextOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

std::shared_future<media_library_return> Configurer::set_overlay_async(const DateTimeOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        auto future = std::async(std::launch::async, [ret]() { return ret; });
        return future.share();
    }
    auto val = m_osd_repository->upsert_overlay_async(overlay);
    return std::async(std::launch::async,
                      [val]() {
                          auto res = val.get();
                          if (!res.has_value())
                          {
                              return res.error();
                          }
                          return MEDIA_LIBRARY_SUCCESS;
                      })
        .share();
}

media_library_return Configurer::set_overlay(const ImageOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::set_overlay(const TextOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::set_overlay(const DateTimeOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Configurer::set_overlay(const CustomOverlay &overlay)
{
    auto ret = set_overlay_in_profile(overlay);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return ret;
    }
    auto val = m_osd_repository->upsert_overlay(overlay);
    if (!val.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set overlay: {}", val.error());
        return val.error();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

} // namespace osd
