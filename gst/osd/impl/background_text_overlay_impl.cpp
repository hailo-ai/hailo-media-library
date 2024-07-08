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

#include "background_text_overlay_impl.hpp"
#include "media_library/media_library_logger.hpp"

BackgroundTextOverlayImpl::BackgroundTextOverlayImpl(const osd::BaseTextOverlay &overlay, media_library_return &status)
    : OverlayImpl(overlay.id, overlay.x, overlay.y, 0, 0, overlay.z_index, overlay.angle, overlay.rotation_alignment_policy, true),
      m_size(0, 0), m_color(overlay.background_color)
{
    status = MEDIA_LIBRARY_SUCCESS;
}

std::shared_ptr<osd::Overlay> BackgroundTextOverlayImpl::get_metadata()
{
    return nullptr;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return>
BackgroundTextOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    /* Background with zero size is invisible */
    if (m_size.width == 0 || m_size.height == 0)
    {
        return std::vector<dsp_overlay_properties_t>();
    }

    set_enabled(false);

    m_image_mat = cv::Mat(m_size, CV_8UC4, cv::Scalar(m_color.blue, m_color.green, m_color.red, m_color.alpha));
    return OverlayImpl::create_dsp_overlays(frame_width, frame_height);
}

cv::Size BackgroundTextOverlayImpl::get_size() const
{
    return m_size;
}

void BackgroundTextOverlayImpl::set_size(cv::Size size)
{
    m_size = size;
}

tl::expected<BackgroundTextOverlayImplPtr, media_library_return> BackgroundTextOverlayImpl::create(const osd::BaseTextOverlay &overlay)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<BackgroundTextOverlayImpl>(overlay, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}