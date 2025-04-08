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

#include "text_overlay_impl.hpp"
#include "media_library/media_library_logger.hpp"
#include <opencv2/core/utils/filesystem.hpp>
#include <opencv2/freetype.hpp>

#define MODULE_NAME LoggerType::Osd

TextOverlayImpl::TextOverlayImpl(const osd::TextOverlay &_overlay, media_library_return &status)
    : TextOverlayImpl(static_cast<const osd::BaseTextOverlay &>(_overlay), status)
{
    change_text(_overlay.label);
}

TextOverlayImpl::TextOverlayImpl(const osd::BaseTextOverlay &_overlay, media_library_return &status)
    : OverlayImpl(_overlay.id, _overlay.x, _overlay.y, 0, 0, _overlay.z_index, _overlay.angle,
                  _overlay.rotation_alignment_policy, true, _overlay.horizontal_alignment, _overlay.vertical_alignment),
      m_text_color(_overlay.text_color), m_background_color(_overlay.background_color), m_font_path(_overlay.font_path),
      m_font_size(_overlay.font_size), m_line_thickness(_overlay.line_thickness), m_shadow_color(_overlay.shadow_color),
      m_shadow_offset_x(_overlay.shadow_offset_x), m_shadow_offset_y(_overlay.shadow_offset_y),
      m_font_weight(_overlay.font_weight), m_outline_size(_overlay.outline_size),
      m_outline_color(_overlay.outline_color)
{
    auto extra_size = cv::Size2f(0, 0);
    auto foreground_text_position = cv::Point2f(0, 0);

    /* Create a copy of the overlay properties */
    auto overlay = _overlay;

    /* If the shadow color is set, create a shadow text overlay */
    if (overlay.shadow_color.red >= 0 && overlay.shadow_color.green >= 0 && overlay.shadow_color.blue >= 0 &&
        overlay.shadow_color.alpha > 0)
    {
        /* If shadow offset is negative, move also the foreground text overlay */
        overlay.x += overlay.shadow_offset_x < 0 ? overlay.shadow_offset_x : 0;
        overlay.y += overlay.shadow_offset_y < 0 ? overlay.shadow_offset_y : 0;

        /* Create a copy of the overlay properties, but change the text color to be the shadow color */
        auto shadow_overlay = overlay;
        shadow_overlay.text_color = overlay.shadow_color;
        shadow_overlay.outline_color = overlay.shadow_color;

        /* To make sure the shadow and foreground text overlays have the same matrix size,
           we'll calculate the added size caused by the shadow offsets */
        extra_size = cv::Size2f(std::abs(overlay.shadow_offset_x), std::abs(overlay.shadow_offset_y));

        /* Calculate the foreground and shadow text positions relative to the enlarged matrix */
        cv::Point2f shadow_text_position(overlay.shadow_offset_x < 0 ? 0 : overlay.shadow_offset_x,
                                         overlay.shadow_offset_y < 0 ? 0 : overlay.shadow_offset_y);
        foreground_text_position = cv::Point2f(overlay.shadow_offset_x > 0 ? 0 : -overlay.shadow_offset_x,
                                               overlay.shadow_offset_y > 0 ? 0 : -overlay.shadow_offset_y);

        auto shadow_text = SimpleTextOverlayImpl::create(shadow_overlay, extra_size, shadow_text_position);
        if (!shadow_text)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create shadow text overlay");
            return;
        }
        m_shadow_text = shadow_text.value();
    }

    /* If the background color is set, create a background text overlay */
    if (overlay.background_color.red >= 0 && overlay.background_color.green >= 0 &&
        overlay.background_color.blue >= 0 && overlay.background_color.alpha > 0)
    {
        auto background = BackgroundTextOverlayImpl::create(overlay);
        if (!background)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create background text overlay");
            return;
        }

        m_background = background.value();
    }

    /* Create the foreground text overlay */
    auto foreground_text = SimpleTextOverlayImpl::create(overlay, extra_size, foreground_text_position);
    if (!foreground_text)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create foreground text overlay");
        return;
    }
    m_foreground_text = foreground_text.value();

    status = MEDIA_LIBRARY_SUCCESS;
}

tl::expected<TextOverlayImplPtr, media_library_return> TextOverlayImpl::create(const osd::TextOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<TextOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

std::shared_future<tl::expected<TextOverlayImplPtr, media_library_return>> TextOverlayImpl::create_async(
    const osd::TextOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]() { return create(overlay); }).share();
}

std::shared_ptr<osd::Overlay> TextOverlayImpl::get_metadata()
{
    auto text_size = m_foreground_text->get_text_size();
    return std::make_shared<osd::TextOverlay>(m_id, m_x, m_y, m_label, m_text_color, m_background_color, m_font_size,
                                              m_line_thickness, m_z_index, m_font_path, m_angle, m_rotation_policy,
                                              m_shadow_color, m_shadow_offset_x, m_shadow_offset_y, m_font_weight,
                                              m_outline_size, m_outline_color, m_horizontal_alignment,
                                              m_vertical_alignment, text_size.width, text_size.height);
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> TextOverlayImpl::create_dsp_overlays(
    int frame_width, int frame_height)
{
    /* If the label hasn't changed, return the previously created overlays */
    if (m_rendered_label == m_label)
    {
        return TextOverlayImpl::get_dsp_overlays();
    }

    std::vector<dsp_overlay_properties_t> dsp_overlays;
    if (m_shadow_text)
    {
        auto status = m_shadow_text->create_dsp_overlays(frame_width, frame_height);
        if (!status)
        {
            return status;
        }
        dsp_overlays.insert(dsp_overlays.end(), status.value().begin(), status.value().end());
    }
    auto status = m_foreground_text->create_dsp_overlays(frame_width, frame_height);
    if (!status)
    {
        return status;
    }
    dsp_overlays.insert(dsp_overlays.end(), status.value().begin(), status.value().end());

    if (m_background)
    {
        /* Shadow (if exists) and foreground text overlays have the same size */
        auto text_size = m_foreground_text->get_text_size();

        /* Rerender the background text overlay if the text size has changed */
        if (m_background->get_size() != text_size)
        {
            m_background->set_size(text_size);
            auto background_overlays = m_background->create_dsp_overlays(frame_width, frame_height);
            if (!background_overlays)
            {
                return background_overlays;
            }
            dsp_overlays.insert(dsp_overlays.begin(), background_overlays->begin(), background_overlays->end());
        }
    }

    m_rendered_label = m_label;
    return dsp_overlays;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> TextOverlayImpl::get_dsp_overlays()
{
    std::vector<dsp_overlay_properties_t> dsp_overlays;
    if (m_background)
    {
        auto background_overlays = m_background->get_dsp_overlays();
        if (!background_overlays)
        {
            return background_overlays;
        }
        dsp_overlays.insert(dsp_overlays.end(), background_overlays->begin(), background_overlays->end());
    }
    if (m_shadow_text)
    {
        auto shadow_overlays = m_shadow_text->get_dsp_overlays();
        if (!shadow_overlays)
        {
            return shadow_overlays;
        }
        dsp_overlays.insert(dsp_overlays.end(), shadow_overlays->begin(), shadow_overlays->end());
    }
    auto foreground_overlays = m_foreground_text->get_dsp_overlays();
    if (!foreground_overlays)
    {
        return foreground_overlays;
    }
    dsp_overlays.insert(dsp_overlays.end(), foreground_overlays->begin(), foreground_overlays->end());

    return dsp_overlays;
}

bool TextOverlayImpl::get_enabled()
{
    return m_foreground_text->get_enabled() && (!m_shadow_text || m_shadow_text->get_enabled()) &&
           (!m_background || m_background->get_enabled());
}

void TextOverlayImpl::set_enabled(bool enabled)
{
    m_foreground_text->set_enabled(enabled);
    if (m_shadow_text)
    {
        m_shadow_text->set_enabled(enabled);
    }
    if (m_background)
    {
        m_background->set_enabled(enabled);
    }
}

void TextOverlayImpl::change_text(const std::string &label)
{
    m_label = label;
    m_foreground_text->change_text(label);
    if (m_shadow_text)
    {
        m_shadow_text->change_text(label);
    }
}
