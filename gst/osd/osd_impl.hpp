/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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
#include "media_library/media_library_logger.hpp"
#include "osd.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <opencv2/freetype.hpp>
#include <set>
#include <unordered_map>
#include <vector>

class OverlayImpl;
using OverlayImplPtr = std::shared_ptr<OverlayImpl>;

class OverlayImpl
{
public:
    OverlayImpl(float x, float y, float width, float height, unsigned int z_index, bool ready_to_blend);
    virtual ~OverlayImpl();

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);

    bool operator<(const OverlayImpl &other);

    std::set<OverlayImplPtr>::iterator get_priority_iterator();
    void set_priority_iterator(std::set<OverlayImplPtr>::iterator priority_iterator);

    virtual std::shared_ptr<osd::Overlay> get_metadata() = 0;
    bool get_ready_to_blend();

protected:
    static tl::expected<std::tuple<int, int>, media_library_return> calc_xy_offsets(
        float x_norm, float y_norm, int overlay_width, int overlay_height, int image_width, int image_height);
    static GstVideoFrame gst_video_frame_from_mat_bgra(cv::Mat mat);
    static media_library_return convert_2_dsp_video_frame(GstVideoFrame *src_frame, GstVideoFrame *dest_frame, GstVideoFormat dest_format);
    static media_library_return create_gst_video_frame(uint width, uint height, std::string format, GstVideoFrame *frame);
    static cv::Mat resize_mat(cv::Mat mat, int width, int height);
    void free_resources();

    cv::Mat m_image_mat;
    std::vector<GstVideoFrame> m_video_frames;
    std::vector<dsp_overlay_properties_t> m_dsp_overlays;

    std::set<OverlayImplPtr>::iterator m_priority_iterator;

    float m_x;
    float m_y;
    float m_width;
    float m_height;
    unsigned int m_z_index;
    bool m_ready_to_blend;
};

class CustomOverlayImpl;
using CustomOverlayImplPtr = std::shared_ptr<CustomOverlayImpl>;

class CustomOverlayImpl : public OverlayImpl
{
public:
    static tl::expected<CustomOverlayImplPtr, media_library_return> create(const osd::CustomOverlay &overlay);
    CustomOverlayImpl(const osd::CustomOverlay &overlay, media_library_return &status);
    virtual ~CustomOverlayImpl() = default;

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);

    virtual std::shared_ptr<osd::Overlay> get_metadata();
};

class ImageOverlayImpl;
using ImageOverlayImplPtr = std::shared_ptr<ImageOverlayImpl>;

class ImageOverlayImpl : public OverlayImpl
{
public:
    static tl::expected<ImageOverlayImplPtr, media_library_return> create(const osd::ImageOverlay &overlay);
    ImageOverlayImpl(const osd::ImageOverlay &overlay, media_library_return &status);
    virtual ~ImageOverlayImpl() = default;

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);

    virtual std::shared_ptr<osd::Overlay> get_metadata();

protected:
    std::string m_path;
};

class TextOverlayImpl;
using TextOverlayImplPtr = std::shared_ptr<TextOverlayImpl>;

class TextOverlayImpl : public OverlayImpl
{
public:
    static tl::expected<TextOverlayImplPtr, media_library_return> create(const osd::TextOverlay &overlay);
    TextOverlayImpl(const osd::TextOverlay &overlay, media_library_return &status);
    void create_text_mat(const osd::TextOverlay &overlay);
    virtual ~TextOverlayImpl() = default;

    virtual std::shared_ptr<osd::Overlay> get_metadata();

protected:
    std::string m_label;
    std::array<int, 3> m_rgb;
    float m_font_size;
    int m_line_thickness;
    std::string m_font_path;
};

class DateTimeOverlayImpl;
using DateTimeOverlayImplPtr = std::shared_ptr<DateTimeOverlayImpl>;

class DateTimeOverlayImpl : public OverlayImpl
{
    /*
    This class does not inherit from TextOverlayImpl because its label is not known at creation time.
    */
public:
    static tl::expected<DateTimeOverlayImplPtr, media_library_return> create(const osd::DateTimeOverlay &overlay);
    DateTimeOverlayImpl(const osd::DateTimeOverlay &overlay, media_library_return &status);
    virtual ~DateTimeOverlayImpl() = default;

    virtual tl::expected<std::vector<dsp_overlay_properties_t>, media_library_return> create_dsp_overlays(int frame_width, int frame_height);
    virtual std::shared_ptr<osd::Overlay> get_metadata();

    static std::string select_chars_for_timestamp();

private:
    std::array<int, 3> m_rgb;
    float m_font_size;
    int m_line_thickness;
    std::string m_datetime_str;
};

namespace osd
{

    class Blender::Impl final
    {
    public:
        static tl::expected<std::unique_ptr<Blender::Impl>, media_library_return> create(const nlohmann::json &config);

        Impl(const nlohmann::json &config, media_library_return &status);
        ~Impl();

        media_library_return set_frame_size(int frame_width, int frame_height);

        media_library_return add_overlay(const std::string &id, const ImageOverlay &overlay);
        media_library_return add_overlay(const std::string &id, const TextOverlay &overlay);
        media_library_return add_overlay(const std::string &id, const DateTimeOverlay &overlay);
        media_library_return add_overlay(const std::string &id, const CustomOverlay &overlay);

        media_library_return remove_overlay(const std::string &id);

        tl::expected<std::shared_ptr<osd::Overlay>, media_library_return> get_overlay(const std::string &id);

        media_library_return set_overlay(const std::string &id, const ImageOverlay &overlay);
        media_library_return set_overlay(const std::string &id, const TextOverlay &overlay);
        media_library_return set_overlay(const std::string &id, const DateTimeOverlay &overlay);
        media_library_return set_overlay(const std::string &id, const CustomOverlay &overlay);

        media_library_return blend(dsp_image_properties_t &input_image_properties);

    private:
        media_library_return add_overlay(const std::string &id, const OverlayImplPtr overlay);
        media_library_return set_overlay(const std::string &id, const OverlayImplPtr overlay);

        void initialize_overlay_images();

        std::unordered_map<std::string, OverlayImplPtr> m_overlays;
        std::set<OverlayImplPtr> m_prioritized_overlays;

        const nlohmann::json m_config;
        std::recursive_mutex m_mutex;

        int m_frame_width;
        int m_frame_height;
    };

}
