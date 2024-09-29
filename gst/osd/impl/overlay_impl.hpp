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

#include "media_library/media_library_types.hpp"
#include "../osd.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <shared_mutex>
#include <opencv2/opencv.hpp>
#include <set>
#include <vector>

#define WIDTH_PADDING 10

class OverlayImpl;
using OverlayImplPtr = std::shared_ptr<OverlayImpl>;

mat_dims internal_calculate_text_size(const std::string &label, const std::string &font_path, int font_size, int line_thickness);

class OverlayImpl
{
public:
    OverlayImpl(std::string id, float x, float y, float width, float height, unsigned int z_index, unsigned int angle, osd::rotation_alignment_policy_t rotation_policy, bool enabled,
                osd::HorizontalAlignment horizontal_alignment, osd::VerticalAlignment vertical_alignment);
    virtual ~OverlayImpl();

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);
    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> get_dsp_overlays();

    bool operator<(const OverlayImpl &other);

    std::set<OverlayImplPtr>::iterator get_priority_iterator();
    void set_priority_iterator(std::set<OverlayImplPtr>::iterator priority_iterator);

    virtual std::shared_ptr<osd::Overlay> get_metadata() = 0;
    virtual bool get_enabled();
    virtual void set_enabled(bool enabled);
    std::string get_id() { return m_id; }

protected:
    static tl::expected<std::tuple<int, int>, media_library_return> calc_xy_offsets(std::string id, float x_norm, float y_norm, size_t overlay_width, size_t overlay_height, int image_width, int image_height, int x_drift, int y_drift,
                                                                                    osd::HorizontalAlignment horizontal_alignment, osd::VerticalAlignment vertical_alignment);
    static GstVideoFrame gst_video_frame_from_mat_bgra(cv::Mat mat);
    static media_library_return convert_2_dma_video_frame(GstVideoFrame *src_frame, GstVideoFrame *dest_frame, GstVideoFormat dest_format);
    static media_library_return create_gst_video_frame(uint width, uint height, std::string format, GstVideoFrame *frame);
    void free_resources();
    static media_library_return create_dma_a420_video_frame(uint width, uint height, GstVideoFrame *frame);
    static media_library_return end_sync_buffer(GstVideoFrame *frame);

    cv::Mat m_image_mat;
    std::vector<GstVideoFrame> m_video_frames;
    std::vector<dsp_overlay_properties_t> m_dsp_overlays;
    std::set<OverlayImplPtr>::iterator m_priority_iterator;

    std::string m_id;
    float m_x;
    float m_y;
    float m_width;
    float m_height;
    unsigned int m_z_index;
    unsigned int m_angle;
    osd::rotation_alignment_policy_t m_rotation_policy;
    bool m_enabled;
    osd::HorizontalAlignment m_horizontal_alignment;
    osd::VerticalAlignment m_vertical_alignment;

    std::shared_mutex m_overlay_mutex;
};