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

#include "media_library/media_library_logger.hpp"
#include "osd_impl.hpp"
#include <algorithm>
#include <vector>

namespace osd
{
    const auto DEFAULT_OSD_CONFIG = R"({
        "osd" : {
            "dateTime" : [
                {
                    "id" : "default_datetime",
                    "font_size" : 70,
                    "font_path" : "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                    "line_thickness" : 3,
                    "rgb" : [ 255, 255, 255 ],
                    "x" : 0.1,
                    "y" : 0.7,
                    "z-index" : 1,
                    "angle": 0,
                    "rotation_policy": "CENTER"
                }
            ],
            "text" : [
                {
                    "id" : "default_text1",
                    "label" : "Hailo",
                    "font_size" : 70,
                    "font_path" : "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                    "line_thickness" : 3,
                    "rgb" : [ 255, 255, 255 ],
                    "x" : 0.7,
                    "y" : 0.7,
                    "z-index" : 1,
                    "angle": 0,
                    "rotation_policy": "CENTER"
                },
                {
                    "id" : "default_text2",
                    "label" : "Stream 0",
                    "font_size" : 70,
                    "font_path" : "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                    "line_thickness" : 3,
                    "rgb" : [ 255, 255, 255 ],
                    "x" : 0.1,
                    "y" : 0.1,
                    "z-index" : 1,
                    "angle": 0,
                    "rotation_policy": "CENTER"
                }
            ]
        }
    })";

    Overlay::Overlay(std::string _id, float _x, float _y, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : id(_id), x(_x), y(_y), z_index(_z_index), angle(_angle), rotation_alignment_policy(_rotation_policy)
    {
    }

    ImageOverlay::ImageOverlay(std::string _id, float _x, float _y, float _width, float _height, std::string _image_path, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy),
                                                                                                                                                                                                                      width(_width), height(_height), image_path(_image_path)
    {
    }

    BaseTextOverlay::BaseTextOverlay() : Overlay("", 0, 0, 1, 0, rotation_alignment_policy_t::CENTER),
                                         label(""), rgb(rgb_color_t()), rgb_background({-1, -1, -1}), font_size(20), line_thickness(1), font_path(DEFAULT_FONT_PATH)
    {
    }

    BaseTextOverlay::BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgb_color_t _rgb, rgb_color_t _rgb_background, float _font_size, int _line_thickness, unsigned int _z_index, std::string font_path, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy),
                                                                                                                                                                                                                                                                                                       label(_label), rgb(_rgb), rgb_background(_rgb_background), font_size(_font_size), line_thickness(_line_thickness), font_path(font_path)
    {
    }

    TextOverlay::TextOverlay() : BaseTextOverlay()
    {
    }

    TextOverlay::TextOverlay(std::string _id, float _x, float _y, std::string _label, rgb_color_t _rgb, rgb_color_t _rgb_background, float _font_size, int _line_thickness, unsigned int _z_index, std::string font_path, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : BaseTextOverlay(_id, _x, _y, _label, _rgb, _rgb_background, _font_size, _line_thickness, _z_index, font_path, _angle, _rotation_policy)
    {
    }

    TextOverlay::TextOverlay(std::string _id, float _x, float _y, std::string _label, rgb_color_t _rgb, rgb_color_t _rgb_background, float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : BaseTextOverlay(_id, _x, _y, _label, _rgb, _rgb_background, _font_size, _line_thickness, _z_index, DEFAULT_FONT_PATH, _angle, _rotation_policy)
    {
    }

    DateTimeOverlay::DateTimeOverlay() : BaseTextOverlay()
    {
    }

    DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, rgb_color_t _rgb, float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : BaseTextOverlay(_id, _x, _y, "", _rgb, {-1, -1, -1}, _font_size, _line_thickness, _z_index, DEFAULT_FONT_PATH, _angle, _rotation_policy)
    {
    }

    DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, rgb_color_t _rgb, rgb_color_t _rgb_background, std::string font_path, float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy) : BaseTextOverlay(_id, _x, _y, "", _rgb, _rgb_background, _font_size, _line_thickness, _z_index, font_path, _angle, _rotation_policy)
    {
    }

    CustomOverlay::CustomOverlay(std::string _id, float _x, float _y, float _width, float _height, DspImagePropertiesPtr buffer, unsigned int _z_index) : Overlay(_id, _x, _y, _z_index, 0, rotation_alignment_policy_t::CENTER),
                                                                                                                                                          width(_width), height(_height), m_buffer(buffer)
    {
    }

    template <typename BasicJsonType>
    inline void from_json(const BasicJsonType &j, rotation_alignment_policy_t &e)
    {
        if (j == "CENTER")
        {
            e = CENTER;
            return;
        }
        if (j == "TOP_LEFT")
        {
            e = TOP_LEFT;
            return;
        }
        LOGGER__ERROR("Unknown enum value received for rotation_alignment_policy_t");
        throw std::invalid_argument("Unknown enum value received for rotation_alignment_policy_t");
    }

    void from_json(const nlohmann::json &json, rgb_color_t &rgb)
    {
        json.at(0).get_to(rgb.red);
        json.at(1).get_to(rgb.green);
        json.at(2).get_to(rgb.blue);
    }

    void from_json(const nlohmann::json &json, ImageOverlay &overlay)
    {
        json.at("id").get_to(overlay.id);
        json.at("x").get_to(overlay.x);
        json.at("y").get_to(overlay.y);
        json.at("width").get_to(overlay.width);
        json.at("height").get_to(overlay.height);
        json.at("image_path").get_to(overlay.image_path);
        json.at("z-index").get_to(overlay.z_index);
        json.at("angle").get_to(overlay.angle);
        json.at("rotation_policy").get_to(overlay.rotation_alignment_policy);
    }

    void from_json(const nlohmann::json &json, TextOverlay &overlay)
    {
        json.at("id").get_to(overlay.id);
        json.at("x").get_to(overlay.x);
        json.at("y").get_to(overlay.y);
        json.at("label").get_to(overlay.label);
        json.at("rgb").get_to(overlay.rgb);
        if (json.find("rgb_background") != json.end())
        {
            json.at("rgb_background").get_to(overlay.rgb_background);
        }
        json.at("font_size").get_to(overlay.font_size);
        json.at("line_thickness").get_to(overlay.line_thickness);
        json.at("z-index").get_to(overlay.z_index);
        json.at("font_path").get_to(overlay.font_path);
        json.at("angle").get_to(overlay.angle);
        json.at("rotation_policy").get_to(overlay.rotation_alignment_policy);
    }

    void from_json(const nlohmann::json &json, DateTimeOverlay &overlay)
    {
        json.at("id").get_to(overlay.id);
        json.at("x").get_to(overlay.x);
        json.at("y").get_to(overlay.y);
        json.at("rgb").get_to(overlay.rgb);
        if (json.find("rgb_background") != json.end())
        {
            json.at("rgb_background").get_to(overlay.rgb_background);
        }
        json.at("font_size").get_to(overlay.font_size);
        json.at("font_path").get_to(overlay.font_path);
        json.at("line_thickness").get_to(overlay.line_thickness);
        json.at("z-index").get_to(overlay.z_index);
        json.at("angle").get_to(overlay.angle);
        json.at("rotation_policy").get_to(overlay.rotation_alignment_policy);
    }

    void from_json(const nlohmann::json &json, CustomOverlay &overlay)
    {
        json.at("id").get_to(overlay.id);
        json.at("x").get_to(overlay.x);
        json.at("y").get_to(overlay.y);
        json.at("width").get_to(overlay.width);
        json.at("height").get_to(overlay.height);
        json.at("z-index").get_to(overlay.z_index);
    }

    tl::expected<std::shared_ptr<Blender>, media_library_return> Blender::create()
    {
        return create(DEFAULT_OSD_CONFIG);
    }

    tl::expected<std::shared_ptr<Blender>, media_library_return> Blender::create(const std::string &config)
    {
        auto impl_expected = Impl::create(config);
        if (impl_expected.has_value())
        {
            return std::make_shared<Blender>(std::move(impl_expected.value()));
        }
        else
        {
            return tl::make_unexpected(impl_expected.error());
        }
    }

    std::shared_future<tl::expected<std::shared_ptr<Blender>, media_library_return>> Blender::create_async()
    {
        return create_async(DEFAULT_OSD_CONFIG);
    }

    std::shared_future<tl::expected<std::shared_ptr<Blender>, media_library_return>> Blender::create_async(const std::string &config)
    {
        return std::async(std::launch::async, [config]()
                          { return create(config); })
            .share();
    }

    Blender::Blender(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
    {
    }

    Blender::~Blender() = default;

    media_library_return Blender::add_overlay(const ImageOverlay &overlay) { return m_impl->add_overlay(overlay); }

    media_library_return Blender::add_overlay(const TextOverlay &overlay) { return m_impl->add_overlay(overlay); }

    media_library_return Blender::add_overlay(const DateTimeOverlay &overlay) { return m_impl->add_overlay(overlay); }

    media_library_return Blender::add_overlay(const CustomOverlay &overlay) { return m_impl->add_overlay(overlay); }

    std::shared_future<media_library_return> Blender::add_overlay_async(const ImageOverlay &overlay) { return m_impl->add_overlay_async(overlay); }

    std::shared_future<media_library_return> Blender::add_overlay_async(const TextOverlay &overlay) { return m_impl->add_overlay_async(overlay); }

    std::shared_future<media_library_return> Blender::add_overlay_async(const DateTimeOverlay &overlay) { return m_impl->add_overlay_async(overlay); }

    std::shared_future<media_library_return> Blender::add_overlay_async(const CustomOverlay &overlay) { return m_impl->add_overlay_async(overlay); }

    tl::expected<std::shared_ptr<Overlay>, media_library_return> Blender::get_overlay(const std::string &id) { return m_impl->get_overlay(id); }

    media_library_return Blender::set_overlay(const ImageOverlay &overlay) { return m_impl->set_overlay(overlay); }

    media_library_return Blender::set_overlay(const TextOverlay &overlay) { return m_impl->set_overlay(overlay); }

    media_library_return Blender::set_overlay(const DateTimeOverlay &overlay) { return m_impl->set_overlay(overlay); }

    media_library_return Blender::set_overlay(const CustomOverlay &overlay) { return m_impl->set_overlay(overlay); }

    std::shared_future<media_library_return> Blender::set_overlay_async(const ImageOverlay &overlay) { return m_impl->set_overlay_async(overlay); }

    std::shared_future<media_library_return> Blender::set_overlay_async(const TextOverlay &overlay) { return m_impl->set_overlay_async(overlay); }

    std::shared_future<media_library_return> Blender::set_overlay_async(const DateTimeOverlay &overlay) { return m_impl->set_overlay_async(overlay); }

    std::shared_future<media_library_return> Blender::set_overlay_async(const CustomOverlay &overlay) { return m_impl->set_overlay_async(overlay); }

    media_library_return Blender::remove_overlay(const std::string &id) { return m_impl->remove_overlay(id); }

    std::shared_future<media_library_return> Blender::remove_overlay_async(const std::string &id) { return m_impl->remove_overlay_async(id); }

    media_library_return Blender::blend(dsp_image_properties_t &input_image_properties) { return m_impl->blend(input_image_properties); }

    media_library_return Blender::set_frame_size(int frame_width, int frame_height) { return m_impl->set_frame_size(frame_width, frame_height); }

    mat_dims calculate_text_size(const std::string &label, const std::string &font_path, int font_size, int line_thickness)
    {
        return internal_calculate_text_size(label, font_path, font_size, line_thickness);
    }
}