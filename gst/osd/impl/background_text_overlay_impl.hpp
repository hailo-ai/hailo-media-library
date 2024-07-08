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
#include "../osd.hpp"

class BackgroundTextOverlayImpl;
using BackgroundTextOverlayImplPtr = std::shared_ptr<BackgroundTextOverlayImpl>;

/* Helper overlay for background color */
class BackgroundTextOverlayImpl : public OverlayImpl
{
public:
    BackgroundTextOverlayImpl(const osd::BaseTextOverlay &overlay, media_library_return &status);
    virtual ~BackgroundTextOverlayImpl() = default;

    virtual std::shared_ptr<osd::Overlay> get_metadata();
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);
    static tl::expected<BackgroundTextOverlayImplPtr, media_library_return> create(const osd::BaseTextOverlay &overlay);

    cv::Size get_size() const;
    void set_size(cv::Size size);

protected:
    cv::Size m_size;
    osd::rgba_color_t m_color;
};