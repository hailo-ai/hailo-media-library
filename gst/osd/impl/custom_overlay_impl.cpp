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

#include "custom_overlay_impl.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "media_library/media_library_logger.hpp"

#define MODULE_NAME LoggerType::Osd

CustomOverlayImpl::CustomOverlayImpl(const osd::CustomOverlay &overlay, media_library_return &status)
    : OverlayImpl(overlay.id, overlay.x, overlay.y, overlay.width, overlay.height, overlay.z_index, overlay.angle,
                  overlay.rotation_alignment_policy, false, overlay.horizontal_alignment, overlay.vertical_alignment)
{
    m_format = overlay.get_format();
    m_is_dsp_buffer_data = false;
    status = MEDIA_LIBRARY_SUCCESS;
}

tl::expected<CustomOverlayImplPtr, media_library_return> CustomOverlayImpl::create(const osd::CustomOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<CustomOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> CustomOverlayImpl::get_dsp_overlays()
{
    if (!get_enabled())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "overlay not ready to blend");
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    if (m_medialib_buffer == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Error: buffer is uninitialized");
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    m_dsp_buffer_data = m_medialib_buffer->buffer_data->As<hailo_dsp_buffer_data_t>();
    m_dsp_overlays[0].overlay = m_dsp_buffer_data.properties;
    return m_dsp_overlays;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> CustomOverlayImpl::create_dsp_overlays(
    int frame_width, int frame_height)
{
    if (frame_width == 0 || frame_height == 0)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    media_library_return status;
    if (!m_dsp_overlays.empty())
    {
        return m_dsp_overlays;
    }

    GstVideoFrame dest_frame;

    std::string format;
    if (m_format == osd::custom_overlay_format::A420)
    {
        format = "A420";
    }
    else if (m_format == osd::custom_overlay_format::ARGB)
    {
        format = "ARGB";
    }
    else
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Error: invalid format {}", m_format);
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    status = create_dma_video_frame(m_width * frame_width, m_height * frame_height, format, &dest_frame);

    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }

    if (m_medialib_buffer != nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Error: m_medialib_buffer is not nullptr");
        m_medialib_buffer.reset();
    }

    m_medialib_buffer = std::make_shared<hailo_media_library_buffer>();
    if (!create_hailo_buffer_from_video_frame(&dest_frame, m_medialib_buffer))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Error: failed to create hailo buffer from video frame");
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    m_video_frames.push_back(dest_frame);
    auto offsets_expected =
        calc_xy_offsets(m_id, m_x, m_y, m_medialib_buffer->buffer_data->width, m_medialib_buffer->buffer_data->height,
                        frame_width, frame_height, 0, 0, m_horizontal_alignment, m_vertical_alignment);
    if (!offsets_expected.has_value())
    {
        return tl::make_unexpected(offsets_expected.error());
    }

    m_dsp_buffer_data = m_medialib_buffer->buffer_data->As<hailo_dsp_buffer_data_t>();
    auto [x_offset, y_offset] = offsets_expected.value();
    dsp_overlay_properties_t dsp_overlay = {
        .overlay = m_dsp_buffer_data.properties,
        .x_offset = x_offset,
        .y_offset = y_offset,
    };

    m_dsp_overlays.push_back(dsp_overlay);

    return m_dsp_overlays;
}

std::shared_ptr<osd::Overlay> CustomOverlayImpl::get_metadata()
{
    return std::make_shared<osd::CustomOverlay>(m_id, m_x, m_y, m_z_index, m_angle, m_rotation_policy,
                                                m_horizontal_alignment, m_vertical_alignment, m_width, m_height,
                                                m_format, m_medialib_buffer);
}
