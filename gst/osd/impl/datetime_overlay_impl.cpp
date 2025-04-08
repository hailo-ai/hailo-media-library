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

#include "datetime_overlay_impl.hpp"
#include "media_library/media_library_logger.hpp"

#define MODULE_NAME LoggerType::Osd

DateTimeOverlayImpl::DateTimeOverlayImpl(const osd::DateTimeOverlay &overlay, media_library_return &status)
    : TextOverlayImpl(overlay, status), m_datetime_format(overlay.datetime_format)
{
    status = MEDIA_LIBRARY_SUCCESS;
}

tl::expected<DateTimeOverlayImplPtr, media_library_return> DateTimeOverlayImpl::create(
    const osd::DateTimeOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<DateTimeOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

std::shared_future<tl::expected<DateTimeOverlayImplPtr, media_library_return>> DateTimeOverlayImpl::create_async(
    const osd::DateTimeOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]() { return create(overlay); }).share();
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> DateTimeOverlayImpl::create_dsp_overlays(
    int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    std::string datetime = select_chars_for_timestamp(m_datetime_format);
    change_text(datetime);
    m_frame_width = frame_width;
    m_frame_height = frame_height;

    return TextOverlayImpl::create_dsp_overlays(frame_width, frame_height);
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> DateTimeOverlayImpl::get_dsp_overlays()
{
    if (!get_enabled())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "overlay not ready to blend");
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    // in case of DateTime, overlay needs to be refreshed with the current time
    create_dsp_overlays(m_frame_width, m_frame_height);

    return TextOverlayImpl::get_dsp_overlays();
}

std::shared_ptr<osd::Overlay> DateTimeOverlayImpl::get_metadata()
{
    auto text_size = m_foreground_text->get_text_size();
    return std::make_shared<osd::DateTimeOverlay>(
        m_id, m_x, m_y, m_datetime_format, m_text_color, m_background_color, m_font_path, m_font_size, m_line_thickness,
        m_z_index, m_angle, m_rotation_policy, m_shadow_color, m_shadow_offset_x, m_shadow_offset_y, m_font_weight,
        m_outline_size, m_outline_color, m_horizontal_alignment, m_vertical_alignment, text_size.width,
        text_size.height);
}

std::string DateTimeOverlayImpl::select_chars_for_timestamp(std::string datetime_format = DEFAULT_DATETIME_STRING)
{
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, datetime_format.c_str());

    if (oss.str().find('%') != std::string::npos)
    {
        LOGGER__MODULE__WARN(MODULE_NAME,
                             "DateTime format string was not interpreted correctly, please check the dateime format");
    }
    return oss.str();
}
