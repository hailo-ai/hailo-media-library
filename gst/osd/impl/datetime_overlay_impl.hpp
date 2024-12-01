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

#include "text_overlay_impl.hpp"

class DateTimeOverlayImpl;
using DateTimeOverlayImplPtr = std::shared_ptr<DateTimeOverlayImpl>;

class DateTimeOverlayImpl : public TextOverlayImpl
{
  public:
    static tl::expected<DateTimeOverlayImplPtr, media_library_return> create(const osd::DateTimeOverlay &overlay);
    static std::shared_future<tl::expected<DateTimeOverlayImplPtr, media_library_return>> create_async(
        const osd::DateTimeOverlay &overlay);
    DateTimeOverlayImpl(const osd::DateTimeOverlay &overlay, media_library_return &status);
    virtual ~DateTimeOverlayImpl() = default;

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> get_dsp_overlays();
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(
        int frame_width, int frame_height);
    virtual std::shared_ptr<osd::Overlay> get_metadata();

    std::string select_chars_for_timestamp(std::string datetime_format);

  private:
    int m_frame_width;
    int m_frame_height;
    std::string m_datetime_format;
};
