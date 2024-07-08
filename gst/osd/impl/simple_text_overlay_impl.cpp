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

SimpleTextOverlayImpl::SimpleTextOverlayImpl(const osd::BaseTextOverlay &overlay,
                                             cv::Size2f extra_size,
                                             cv::Point2f text_position,
                                             media_library_return &status)
    : OverlayImpl(overlay.id, overlay.x, overlay.y, 0, 0, overlay.z_index, overlay.angle, overlay.rotation_alignment_policy, true),
      m_label(""), m_rgba_text_color{(double)overlay.text_color.red, (double)overlay.text_color.green,
                                     (double)overlay.text_color.blue, (double)overlay.text_color.alpha},
      m_font_size(overlay.font_size), m_line_thickness(overlay.line_thickness), m_font_path(overlay.font_path),
      m_extra_size(extra_size), m_text_position(text_position)
{
    if (!cv::utils::fs::exists(m_font_path))
    {
        LOGGER__ERROR("Error: file {} does not exist", m_font_path);
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    ft2 = cv::freetype::createFreeType2();
    ft2->loadFontData(m_font_path, 0);

    status = MEDIA_LIBRARY_SUCCESS;
}

tl::expected<SimpleTextOverlayImplPtr, media_library_return> SimpleTextOverlayImpl::create(const osd::BaseTextOverlay &overlay, cv::Size2f extra_size, cv::Point2f text_position)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    auto osd_overlay = std::make_shared<SimpleTextOverlayImpl>(overlay, extra_size, text_position, status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return osd_overlay;
}

std::shared_ptr<osd::Overlay> SimpleTextOverlayImpl::get_metadata()
{
    // This is an internal class, so we don't need to return metadata
    return nullptr;
}

tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> SimpleTextOverlayImpl::create_dsp_overlays(int frame_width, int frame_height)
{
    // If label is empty, display nothing
    if (m_label == "")
    {
        return std::vector<dsp_overlay_properties_t>();
    }

    set_enabled(false);
    create_text_m_mat(frame_width, frame_height);
    return OverlayImpl::create_dsp_overlays(frame_width, frame_height);
}

cv::Size SimpleTextOverlayImpl::calculate_text_size(cv::Size cv_text_size, int &baseline, int frame_width, int frame_height)
{
    cv::Size extra_size = cv::Size(m_extra_size.width * frame_width, m_extra_size.height * frame_height);
    cv::Size final_size = cv_text_size + extra_size;

    /* Round up to even numbers to support YUV420 */
    final_size.width += final_size.width % 2;
    final_size.height += final_size.height % 2;

    baseline += baseline % 2;
    final_size.height += baseline;

    return final_size;
}

media_library_return SimpleTextOverlayImpl::create_text_m_mat(int frame_width, int frame_height)
{
    /* calculate text size */
    int baseline;
    cv::Size cv_text_size = ft2->getTextSize(m_label, m_font_size, m_line_thickness, &baseline);
    cv::Size text_size = calculate_text_size(cv_text_size, baseline, frame_width, frame_height);

    /* Calculate text position by adjusting according to m_text_position */
    auto text_position_offset = cv::Point(m_text_position.x * frame_width, m_text_position.y * frame_height);
    auto text_position = cv::Point(0, text_size.height - baseline - m_extra_size.height * frame_height) + text_position_offset;

    /* Write text into RGB matrix.
       Use white background if text is black, otherwise use black background, to differentiate text from background */
    cv::Scalar rgb_color = cv::Scalar(m_rgba_text_color[0], m_rgba_text_color[1], m_rgba_text_color[2]);
    bool is_text_black = rgb_color == cv::Scalar(0, 0, 0);
    cv::Scalar background_color_rgb = is_text_black ? cv::Scalar(255, 255, 255) : cv::Scalar(0, 0, 0);
    cv::Mat rgb_mat = cv::Mat(text_size, CV_8UC3, background_color_rgb);
    ft2->putText(rgb_mat, m_label, text_position, m_font_size, rgb_color, cv::FILLED, 8, true);

    /* Convert from RGB to BGRA.
       This code is using pointers for auto vectorization of the compiler */
    m_image_mat = cv::Mat(text_size, CV_8UC4);
    uint8_t *bgra_ptr = m_image_mat.ptr<uint8_t>();
    uint8_t *rgb_ptr = rgb_mat.ptr<uint8_t>();
    uint8_t text_r = m_rgba_text_color[0];
    uint8_t text_g = m_rgba_text_color[1];
    uint8_t text_b = m_rgba_text_color[2];
    uint8_t text_a = m_rgba_text_color[3];
    for (int i = 0; i < text_size.area(); i++)
    {
        uint8_t r = rgb_ptr[i * 3 + 0];
        uint8_t g = rgb_ptr[i * 3 + 1];
        uint8_t b = rgb_ptr[i * 3 + 2];
        uint8_t a = (r == text_r && g == text_g && b == text_b) ? text_a : 0;

        bgra_ptr[4 * i + 0] = b;
        bgra_ptr[4 * i + 1] = g;
        bgra_ptr[4 * i + 2] = r;
        bgra_ptr[4 * i + 3] = a;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

/* Text will be rendered with the new label only after calling create_dsp_overlays */
void SimpleTextOverlayImpl::change_text(const std::string &label)
{
    m_label = label;
}

cv::Size SimpleTextOverlayImpl::get_text_size() const
{
    return m_image_mat.size();
}