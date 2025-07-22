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

class CustomOverlayImpl;
using CustomOverlayImplPtr = std::shared_ptr<CustomOverlayImpl>;

class CustomOverlayImpl : public OverlayImpl
{
  public:
    static tl::expected<CustomOverlayImplPtr, media_library_return> create(const osd::CustomOverlay &overlay);
    CustomOverlayImpl(const osd::CustomOverlay &overlay, media_library_return &status);
    virtual ~CustomOverlayImpl() = default;

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(
        int frame_width, int frame_height);
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> get_dsp_overlays();

    virtual std::shared_ptr<osd::Overlay> get_metadata();
    virtual HailoMediaLibraryBufferPtr get_buffer()
    {
        return m_medialib_buffer;
    }

  protected:
    osd::custom_overlay_format m_format;
    HailoMediaLibraryBufferPtr m_medialib_buffer;
    hailo_dsp_buffer_data_t m_dsp_buffer_data;
};
