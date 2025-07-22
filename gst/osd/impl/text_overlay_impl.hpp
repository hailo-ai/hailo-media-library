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

#include "../osd.hpp"
#include "background_text_overlay_impl.hpp"
#include "overlay_impl.hpp"
#include "simple_text_overlay_impl.hpp"
#include <optional>

class TextOverlayImpl;
using TextOverlayImplPtr = std::shared_ptr<TextOverlayImpl>;
class TextOverlayImpl : public OverlayImpl
{
  public:
    static tl::expected<TextOverlayImplPtr, media_library_return> create(const osd::TextOverlay &overlay);
    TextOverlayImpl(const osd::TextOverlay &overlay, media_library_return &status);
    TextOverlayImpl(const osd::BaseTextOverlay &overlay, media_library_return &status);
    virtual ~TextOverlayImpl() = default;

    virtual std::shared_ptr<osd::Overlay> get_metadata();
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(
        int frame_width, int frame_height);
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> get_dsp_overlays();

    virtual bool get_enabled();
    virtual void set_enabled(bool enabled);

    void change_text(const std::string &label);

  protected:
    SimpleTextOverlayImplPtr m_foreground_text;
    SimpleTextOverlayImplPtr m_shadow_text;
    BackgroundTextOverlayImplPtr m_background;

    std::string m_label;
    std::string m_rendered_label;

    osd::rgba_color_t m_text_color;
    osd::rgba_color_t m_background_color;

    std::string m_font_path;
    float m_font_size;
    int m_line_thickness;

    osd::rgba_color_t m_shadow_color;
    float m_shadow_offset_x;
    float m_shadow_offset_y;

    osd::font_weight_t m_font_weight;

    int m_outline_size;
    osd::rgba_color_t m_outline_color;
};
