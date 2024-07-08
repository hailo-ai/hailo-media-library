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

#pragma once

#include "overlay_impl.hpp"
#include <opencv2/freetype.hpp>

class SimpleTextOverlayImpl;
using SimpleTextOverlayImplPtr = std::shared_ptr<SimpleTextOverlayImpl>;

class SimpleTextOverlayImpl : public OverlayImpl
{
public:
    static tl::expected<SimpleTextOverlayImplPtr, media_library_return> create(const osd::BaseTextOverlay &overlay, cv::Size2f extra_size, cv::Point2f text_position);
    SimpleTextOverlayImpl(const osd::BaseTextOverlay &overlay, cv::Size2f extra_size, cv::Point2f text_position, media_library_return &status);
    virtual ~SimpleTextOverlayImpl() = default;

    virtual std::shared_ptr<osd::Overlay> get_metadata();
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);

    void change_text(const std::string &label);
    cv::Size get_text_size() const;

protected:
    cv::Size calculate_text_size(cv::Size cv_text_size, int &baseline, int frame_width, int frame_height);
    media_library_return create_text_m_mat(int frame_width, int frame_height);
    cv::Ptr<cv::freetype::FreeType2> ft2;
    std::string m_label;
    cv::Scalar m_rgba_text_color;
    float m_font_size;
    int m_line_thickness;
    std::string m_font_path;
    cv::Size2f m_extra_size;
    cv::Point2f m_text_position;
};