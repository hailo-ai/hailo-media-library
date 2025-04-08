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

#include "blender_impl.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include <algorithm>
#include <thread>
#include <iomanip>
#include "custom_overlay_impl.hpp"
#include "image_overlay_impl.hpp"
#include "text_overlay_impl.hpp"
#include "datetime_overlay_impl.hpp"

#define MODULE_NAME LoggerType::Osd

mat_dims internal_calculate_text_size(const std::string &label, const std::string &font_path, int font_size,
                                      int line_thickness)
{
    if (!cv::utils::fs::exists(font_path))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Error: file {} does not exist", font_path);
        return {0, 0, 0};
    }

    cv::Ptr<cv::freetype::FreeType2> ft2;
    ft2 = cv::freetype::createFreeType2();
    ft2->loadFontData(font_path, 0);

    int baseline = 0;
    cv::Size text_size = ft2->getTextSize(label, font_size, line_thickness, &baseline);

    text_size.width += text_size.width % 2;
    text_size.height += text_size.height % 2;

    baseline += baseline % 2;

    return {text_size.width + WIDTH_PADDING, text_size.height + baseline, baseline};
}

namespace osd
{
tl::expected<std::unique_ptr<Blender::Impl>, media_library_return> Blender::Impl::create(const std::string &config)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto blender = std::unique_ptr<Blender::Impl>(new Blender::Impl(config, status));

    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return blender;
}

std::shared_future<tl::expected<std::unique_ptr<Blender::Impl>, media_library_return>> Blender::Impl::create_async(
    const std::string &config)
{
    return std::async(std::launch::async, [&config]() { return create(config); }).share();
}

media_library_return Blender::Impl::configure(const std::string &config)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;

    // check if the config has ' at the beginning and end of the string. if so, remove them
    std::string clean_config = config; // config is const, so we need to copy it
    if (clean_config[0] == '\'' && clean_config[clean_config.size() - 1] == '\'')
    {
        clean_config = clean_config.substr(1, config.size() - 2);
    }

    auto ret = m_config_manager->validate_configuration(clean_config);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate configuration: {}", ret);
        status = MEDIA_LIBRARY_CONFIGURATION_ERROR;
        return status;
    }
    std::vector<std::string> keys(m_overlays.size());
    std::transform(m_overlays.begin(), m_overlays.end(), keys.begin(), [](const auto &pair) { return pair.first; });

    for (const auto &key : keys)
    {
        remove_overlay(key);
    }
    m_config = nlohmann::json::parse(clean_config)["osd"];
    if (m_config.contains("image"))
    {
        for (auto &image_json : m_config["image"])
        {
            auto overlay = image_json.template get<ImageOverlay>();
            add_overlay(overlay);
        }
    }
    if (m_config.contains("text"))
    {
        for (auto &text_json : m_config["text"])
        {
            auto overlay = text_json.template get<TextOverlay>();
            add_overlay(overlay);
        }
    }

    if (m_config.contains("dateTime"))
    {
        for (auto &datetime_json : m_config["dateTime"])
        {
            auto overlay = datetime_json.template get<DateTimeOverlay>();
            add_overlay(overlay);
        }
    }

    if (m_config.contains("custom"))
    {
        for (auto &custom_json : m_config["custom"])
        {
            auto overlay = custom_json.template get<CustomOverlay>();
            add_overlay(overlay);
        }
    }

    status = MEDIA_LIBRARY_SUCCESS;
    return status;
}

Blender::Impl::Impl(const std::string &config, media_library_return &status)
{
    m_frame_width = 0;
    m_frame_height = 0;
    m_frame_size_set = false;
    m_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_OSD);

    // Acquire DSP device
    dsp_status dsp_result = dsp_utils::acquire_device();
    if (dsp_result != DSP_SUCCESS)
    {
        status = MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        LOGGER__MODULE__ERROR(MODULE_NAME, "Accuire DSP device failed with status code {}", status);
        return;
    }
    status = configure(config);
}

Blender::Impl::~Impl()
{
    m_prioritized_overlays.clear();
    m_overlays.clear();

    dsp_status dsp_result = dsp_utils::release_device();
    if (dsp_result != DSP_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Release DSP device failed with status code {}", dsp_result);
    }
}

std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const DateTimeOverlay &overlay)
{
    return std::async(std::launch::async,
                      [this, overlay]() {
                          auto overlay_result = DateTimeOverlayImpl::create_async(overlay);
                          auto overlay_expected = overlay_result.get();
                          if (!overlay_expected.has_value())
                          {
                              LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create datetime overlay {}", overlay.id);
                              return overlay_expected.error();
                          }

                          return add_overlay(overlay_expected.value());
                      })
        .share();
}

std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const ImageOverlay &overlay)
{
    return std::async(std::launch::async,
                      [this, overlay]() {
                          auto overlay_result = ImageOverlayImpl::create_async(overlay);
                          auto overlay_expected = overlay_result.get();
                          if (!overlay_expected.has_value())
                          {
                              LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create image overlay {}", overlay.id);
                              return overlay_expected.error();
                          }

                          return add_overlay(overlay_expected.value());
                      })
        .share();
}

std::shared_future<media_library_return> Blender::Impl::add_overlay_async(const TextOverlay &overlay)
{
    return std::async(std::launch::async,
                      [this, overlay]() {
                          auto overlay_result = TextOverlayImpl::create_async(overlay);
                          auto overlay_expected = overlay_result.get();
                          if (!overlay_expected.has_value())
                          {
                              LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create text overlay {}", overlay.id);
                              return overlay_expected.error();
                          }

                          return add_overlay(overlay_expected.value());
                      })
        .share();
}

media_library_return Blender::Impl::add_overlay(const ImageOverlay &overlay)
{
    auto overlay_expected = ImageOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create image overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return add_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::add_overlay(const TextOverlay &overlay)
{
    auto overlay_expected = TextOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create text overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return add_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::add_overlay(const DateTimeOverlay &overlay)
{
    auto overlay_expected = DateTimeOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create datetime overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return add_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::add_overlay(const CustomOverlay &overlay)
{
    auto overlay_expected = CustomOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create custom overlay {}", overlay.id);
        return overlay_expected.error();
    }

    // allocate the custom overlay buffer
    if (!m_frame_size_set)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Frame size is not set");
        return MEDIA_LIBRARY_UNINITIALIZED;
    }

    auto create_dsp_overlay_expected = overlay_expected.value()->create_dsp_overlays(m_frame_width, m_frame_height);

    if (!create_dsp_overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create custom overlay {}", overlay.id);
        return create_dsp_overlay_expected.error();
    }
    return add_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::set_overlay_enabled(const std::string &id, bool enabled)
{
    std::unique_lock lock(m_mutex);
    if (!m_overlays.contains(id))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No overlay with id {}", id);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    m_overlays[id]->set_enabled(enabled);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Blender::Impl::add_overlay(const OverlayImplPtr overlay)
{
    if (m_overlays.contains(overlay->get_id()))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay with id {} already exists", overlay->get_id());
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_frame_size_set) // if frame size is not set, the overlays will be initialized when the frame size is set
    {
        auto ret = overlay->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            return ret.error();
        }
    }

    std::unique_lock ulock(m_mutex);
    return add_overlay_internal(overlay);
}

// this method is not thread safe, the caller should have a mutex locked
media_library_return Blender::Impl::add_overlay_internal(const OverlayImplPtr overlay)
{
    if (m_overlays.contains(overlay->get_id()))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Overlay with id {} already exists", overlay->get_id());
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Inserting overlay with id {}", overlay->get_id());

    auto result1 = m_overlays.insert({overlay->get_id(), overlay});
    if (!result1.second)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to insert overlay with id {}", overlay->get_id());
        return MEDIA_LIBRARY_ERROR;
    }

    auto result2 = m_prioritized_overlays.insert(overlay);
    if (!result2.second)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to insert overlay with id {}", overlay->get_id());
        m_overlays.erase(overlay->get_id());
        return MEDIA_LIBRARY_ERROR;
    }
    result1.first->second->set_priority_iterator(result2.first);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Blender::Impl::remove_overlay(const std::string &id)
{
    if (!m_overlays.contains(id))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No overlay with id {}", id);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    std::unique_lock lock(m_mutex);
    return remove_overlay_internal(id);
}

// this method is not thread safe, the caller should have a mutex locked
media_library_return Blender::Impl::remove_overlay_internal(const std::string &id)
{
    if (!m_overlays.contains(id))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No overlay with id {}", id);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing overlay with id {}", id);

    m_prioritized_overlays.erase(m_overlays[id]->get_priority_iterator());
    m_overlays.erase(id);
    return MEDIA_LIBRARY_SUCCESS;
}

std::shared_future<media_library_return> Blender::Impl::remove_overlay_async(const std::string &id)
{
    return std::async(std::launch::async, [this, id]() { return remove_overlay(id); }).share();
}

tl::expected<std::shared_ptr<osd::Overlay>, media_library_return> Blender::Impl::get_overlay(const std::string &id)
{
    std::shared_lock lock(m_mutex);
    if (!m_overlays.contains(id))
    {
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }
    auto overlay = m_overlays[id];
    return m_overlays[id]->get_metadata();
}

std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const ImageOverlay &overlay)
{
    return std::async(std::launch::async, [this, overlay]() {
        auto overlay_result = ImageOverlayImpl::create_async(overlay);
        auto overlay_expected = overlay_result.get();
        if (!overlay_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set ImageOverlay {}", overlay.id);
            return overlay_expected.error();
        }

        return set_overlay(overlay_expected.value());
    });
}

std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const TextOverlay &overlay)
{
    return std::async(std::launch::async,
                      [this, overlay]() {
                          auto overlay_result = TextOverlayImpl::create_async(overlay);
                          auto overlay_expected = overlay_result.get();
                          if (!overlay_expected.has_value())
                          {
                              LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set TextOverlay {}", overlay.id);
                              return overlay_expected.error();
                          }

                          return set_overlay(overlay_expected.value());
                      })
        .share();
}

std::shared_future<media_library_return> Blender::Impl::set_overlay_async(const DateTimeOverlay &overlay)
{
    return std::async(std::launch::async,
                      [this, overlay]() {
                          auto overlay_result = DateTimeOverlayImpl::create_async(overlay);
                          auto overlay_expected = overlay_result.get();
                          if (!overlay_expected.has_value())
                          {
                              LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set DateTimeOverlay {}", overlay.id);
                              return overlay_expected.error();
                          }

                          return set_overlay(overlay_expected.value());
                      })
        .share();
}

media_library_return Blender::Impl::set_overlay(const ImageOverlay &overlay)
{
    auto overlay_expected = ImageOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set text overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return set_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::set_overlay(const TextOverlay &overlay)
{
    auto overlay_expected = TextOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set text overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return set_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::set_overlay(const DateTimeOverlay &overlay)
{
    auto overlay_expected = DateTimeOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set datettime overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return set_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::set_overlay(const CustomOverlay &overlay)
{
    auto overlay_expected = CustomOverlayImpl::create(overlay);
    if (!overlay_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set custom overlay {}", overlay.id);
        return overlay_expected.error();
    }

    return set_overlay(overlay_expected.value());
}

media_library_return Blender::Impl::set_overlay(const OverlayImplPtr overlay)
{
    if (!m_overlays.contains(overlay->get_id()))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No overlay with id {}", overlay->get_id());
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_frame_size_set) // if frame size is not set, the overlays will be initialized when the frame size is set
    {
        auto ret = overlay->create_dsp_overlays(m_frame_width, m_frame_height);
        if (!ret.has_value())
        {
            return ret.error();
        }
    }

    std::unique_lock lock(m_mutex);
    if (remove_overlay_internal(overlay->get_id()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to remove overlay with id {}", overlay->get_id());
        return MEDIA_LIBRARY_ERROR;
    }

    return add_overlay_internal(overlay);
}

media_library_return Blender::Impl::blend(HailoMediaLibraryBufferPtr &input_buffer)
{
    std::unique_lock lock(m_mutex);

    // We prepare to blend all overlays at once
    std::vector<dsp_overlay_properties_t> all_overlays_to_blend;
    all_overlays_to_blend.reserve(m_overlays.size());
    for (const auto &overlay : m_prioritized_overlays)
    {
        if (!overlay->get_enabled())
        {
            continue;
        }

        auto dsp_overlays_expected = overlay->get_dsp_overlays();
        if (!dsp_overlays_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get DSP compatible overlays ({})",
                                  dsp_overlays_expected.error());
            return dsp_overlays_expected.error();
        }
        auto dsp_overlays = dsp_overlays_expected.value();

        all_overlays_to_blend.insert(all_overlays_to_blend.end(), dsp_overlays.begin(), dsp_overlays.end());
    }

    // DSP blend have better color quality when the overlays have even x and y offsets
    for (auto &dsp_overlay : all_overlays_to_blend)
    {
        dsp_overlay.x_offset -= dsp_overlay.x_offset % 2;
        dsp_overlay.y_offset -= dsp_overlay.y_offset % 2;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Blending {} overlays", all_overlays_to_blend.size());

    // Perform blending for all overlays
    for (unsigned int i = 0; i < all_overlays_to_blend.size(); i += dsp_utils::max_blend_overlays)
    {
        auto first = all_overlays_to_blend.begin() + i;
        auto last = all_overlays_to_blend.end();

        if (i + dsp_utils::max_blend_overlays < all_overlays_to_blend.size())
        {
            last = first + dsp_utils::max_blend_overlays;
        }

        std::vector blend_chuck(first, last);

        dsp_status status =
            dsp_utils::perform_dsp_multiblend(input_buffer->buffer_data.get(), blend_chuck.data(), blend_chuck.size());

        if (status != DSP_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "DSP blend failed with {}", status);
            return MEDIA_LIBRARY_DSP_OPERATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Blender::Impl::set_frame_size(int frame_width, int frame_height)
{
    std::unique_lock lock(m_mutex);

    if (frame_width < 1 || frame_height < 1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Frame size is invalid ({}x{})", frame_width, frame_height);
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    if (m_frame_width == frame_width && m_frame_height == frame_height)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Frame size is already set to {}x{}", frame_width, frame_height);
        return MEDIA_LIBRARY_SUCCESS;
    }

    m_frame_width = frame_width;
    m_frame_height = frame_height;
    m_frame_size_set = true;

    // Initialize static images
    std::vector<dsp_overlay_properties_t> overlays;
    overlays.reserve(m_overlays.size());
    for (const auto &overlay : m_prioritized_overlays)
    {
        auto overlays_expected = overlay->create_dsp_overlays(frame_width, frame_height);
        if (!overlays_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to prepare overlays ({})", overlays_expected.error());
            return overlays_expected.error();
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}
} // namespace osd
