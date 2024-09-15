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

CustomOverlayImpl::CustomOverlayImpl(const osd::CustomOverlay &overlay, media_library_return &status)
    : OverlayImpl(overlay.id, overlay.x, overlay.y, overlay.width, overlay.height, overlay.z_index, overlay.angle,
                  overlay.rotation_alignment_policy, false, overlay.horizontal_alignment, overlay.vertical_alignment)
{
    m_format = overlay.get_format();
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

std::shared_future<tl::expected<CustomOverlayImplPtr, media_library_return>> CustomOverlayImpl::create_async(const osd::CustomOverlay &overlay)
{
    return std::async(std::launch::async, [overlay]()
                      { return create(overlay); })
        .share();
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> CustomOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
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
        LOGGER__ERROR("Error: invalid format {}", m_format);
        return tl::make_unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }
    status = create_gst_video_frame(m_width * frame_width, m_height * frame_height, format, &dest_frame);

    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }

    dsp_image_properties_t dsp_image;
    create_dsp_buffer_from_video_frame(&dest_frame, dsp_image);
    m_video_frames.push_back(dest_frame);
    auto offsets_expected = calc_xy_offsets(m_id, m_x, m_y, dsp_image.width, dsp_image.height, frame_width,
                                            frame_height, 0, 0, m_horizontal_alignment, m_vertical_alignment);
    if (!offsets_expected.has_value())
    {
        return tl::make_unexpected(offsets_expected.error());
    }

    auto [x_offset, y_offset] = offsets_expected.value();
    dsp_overlay_properties_t dsp_overlay =
        {
            .overlay = dsp_image,
            .x_offset = x_offset,
            .y_offset = y_offset,
        };

    m_dsp_overlays.push_back(dsp_overlay);

    return m_dsp_overlays;
}

std::shared_ptr<osd::Overlay> CustomOverlayImpl::get_metadata()
{
    DspImagePropertiesPtr dsp_image = std::make_shared<dsp_image_properties_t>(m_dsp_overlays[0].overlay);
    return std::make_shared<osd::CustomOverlay>(m_id, m_x, m_y, m_z_index, m_angle, m_rotation_policy,
                                                m_horizontal_alignment, m_vertical_alignment, m_width, m_height,
                                                m_format, dsp_image);
}