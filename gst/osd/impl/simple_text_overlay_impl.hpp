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
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>
#include <freetype/freetype.h>
#include <freetype/ftstroke.h>

#include <utility>
#include <memory>

using ft_library_ptr = std::unique_ptr<FT_LibraryRec_, decltype(&FT_Done_FreeType)>;
using ft_face_ptr = std::unique_ptr<FT_FaceRec_, decltype(&FT_Done_Face)>;
using ft_stroker_ptr = std::unique_ptr<FT_StrokerRec_, decltype(&FT_Stroker_Done)>;
using hb_font_ptr = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
using hb_buffer_ptr = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>;

using size_baseline = std::pair<cv::Size, int>;

class SimpleTextOverlayImpl;
using SimpleTextOverlayImplPtr = std::shared_ptr<SimpleTextOverlayImpl>;
class SimpleTextOverlayImpl final : public OverlayImpl
{
  public:
    static tl::expected<SimpleTextOverlayImplPtr, media_library_return> create(const osd::BaseTextOverlay &overlay,
                                                                               cv::Size2f extra_size,
                                                                               cv::Point2f text_position);
    SimpleTextOverlayImpl(const osd::BaseTextOverlay &overlay, cv::Size2f extra_size, cv::Point2f text_position,
                          media_library_return &status);
    virtual ~SimpleTextOverlayImpl() = default;

    virtual std::shared_ptr<osd::Overlay> get_metadata();
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(
        int frame_width, int frame_height);

    void change_text(const std::string &label);
    cv::Size get_text_size() const;

  private:
    media_library_return create_text_m_mat(int frame_width, int frame_height);
    tl::expected<size_baseline, media_library_return> get_text_size_baseline(int frame_width, int frame_height);
    media_library_return put_text(cv::Mat dst, cv::Point org);
    void put_glyph(cv::Mat dst, FT_Bitmap *bmp, cv::Point glyph_position, cv::Scalar color);

    std::string m_label;
    cv::Scalar m_rgba_text_color;
    cv::Scalar m_rgba_outline_color;
    std::string m_font_path;
    float m_font_size;
    int m_outline_size;
    osd::font_weight_t m_font_weight;
    cv::Size2f m_extra_size;
    cv::Point2f m_text_position;
    FT_F26Dot6 m_first_glyph_left_bearing;

    hb_font_ptr m_hb_font;
    hb_buffer_ptr m_hb_buffer;
    ft_library_ptr m_ft_library;
    ft_face_ptr m_ft_face;
    ft_stroker_ptr m_ft_stroker;
};
