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

#include "osd_impl.hpp"
#include <vector>
#include <algorithm>
#include "media_library/media_library_logger.hpp"

namespace osd
{

Overlay::Overlay(float _x, float _y, unsigned int _z_index) :
    x(_x), y(_y), z_index(_z_index)
{ }

ImageOverlay::ImageOverlay(float _x, float _y, float _width, float _height, std::string _image_path, unsigned int _z_index) :
    Overlay(_x, _y, _z_index),
    width(_width), height(_height), image_path(_image_path)
{}

TextOverlay::TextOverlay() : Overlay(0, 0, 1),
    label(""), rgb(RGBColor()), font_size(20), line_thickness(1), font_path(DEFAULT_FONT_PATH)
{}

TextOverlay::TextOverlay(float _x, float _y, std::string _label, RGBColor _rgb, float _font_size, int _line_thickness, unsigned int _z_index, std::string font_path) : Overlay(_x, _y, _z_index),
    label(_label), rgb(_rgb), font_size(_font_size), line_thickness(_line_thickness), font_path(font_path)
{}

TextOverlay::TextOverlay(float _x, float _y, std::string _label, RGBColor _rgb, float _font_size, int _line_thickness, unsigned int _z_index) : Overlay(_x, _y, _z_index),
    label(_label), rgb(_rgb), font_size(_font_size), line_thickness(_line_thickness), font_path(DEFAULT_FONT_PATH)
{}

DateTimeOverlay::DateTimeOverlay() : Overlay(0.1, 0.1, 3), 
    rgb(RGBColor()), font_size(2), line_thickness(1), font_path(DEFAULT_FONT_PATH)
{}
DateTimeOverlay::DateTimeOverlay(float _x, float _y, RGBColor _rgb, float _font_size, int _line_thickness, unsigned int _z_index, std::string font_path) : Overlay(_x, _y, _z_index),
    rgb(_rgb), font_size(_font_size), line_thickness(_line_thickness), font_path(font_path)
{}
DateTimeOverlay::DateTimeOverlay(float _x, float _y, RGBColor _rgb, float _font_size, int _line_thickness, unsigned int _z_index) : Overlay(_x, _y, _z_index),
    rgb(_rgb), font_size(_font_size), line_thickness(_line_thickness), font_path(DEFAULT_FONT_PATH)
{}
CustomOverlay::CustomOverlay(float _x, float _y, float _width, float _height, DspImagePropertiesPtr buffer, unsigned int _z_index) : Overlay(_x, _y, _z_index),
    width(_width), height(_height), m_buffer(buffer)
{}

void from_json(const nlohmann::json& json, RGBColor& rgb)
{
    json.at(0).get_to(rgb.red);
    json.at(1).get_to(rgb.green);
    json.at(2).get_to(rgb.blue);
}

void from_json(const nlohmann::json& json, ImageOverlay& overlay)
{
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json.at("width").get_to(overlay.width);
    json.at("height").get_to(overlay.height);
    json.at("image_path").get_to(overlay.image_path);
    json.at("z-index").get_to(overlay.z_index);
}

void from_json(const nlohmann::json& json, TextOverlay& overlay)
{
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json.at("label").get_to(overlay.label);
    json.at("rgb").get_to(overlay.rgb);
    json.at("font_size").get_to(overlay.font_size);
    json.at("line_thickness").get_to(overlay.line_thickness);
    json.at("z-index").get_to(overlay.z_index);
}

void from_json(const nlohmann::json& json, DateTimeOverlay& overlay)
{
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json.at("rgb").get_to(overlay.rgb);
    json.at("font_size").get_to(overlay.font_size);
    json.at("line_thickness").get_to(overlay.line_thickness);
    json.at("z-index").get_to(overlay.z_index);
}

void from_json(const nlohmann::json& json, CustomOverlay& overlay)
{
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json.at("width").get_to(overlay.width);
    json.at("height").get_to(overlay.height);
    json.at("z-index").get_to(overlay.z_index);
}

tl::expected<std::shared_ptr<Blender>, media_library_return> Blender::create() {
    return create(R"({
        "dateTime" : [
            {
                "id" : "default_datetime",
                "font_size" : 1.0,
                "line_thickness" : 1,
                "rgb" : [ 255, 255, 255 ],
                "x" : 0.6,
                "y" : 0.1,
                "z-index" : 1
            }
        ],
        "text" : [
            {
                "id" : "default_text1",
                "label" : "Hailo",
                "font_size" : 1.0,
                "line_thickness" : 1,
                "rgb" : [ 255, 255, 255 ],
                "x" : 0.7,
                "y" : 0.7,
                "z-index" : 1
            },
            {
                "id" : "default_text2",
                "label" : "Stream 0",
                "font_size" : 1.0,
                "line_thickness" : 1,
                "rgb" : [ 255, 255, 255 ],
                "x" : 0.1,
                "y" : 0.1,
                "z-index" : 1
            }
        ]
    })"_json);
}

tl::expected<std::shared_ptr<Blender>, media_library_return> Blender::create(const nlohmann::json& config)
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

Blender::Blender(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{ }

Blender::~Blender() = default;

media_library_return Blender::add_overlay(const std::string& id, const ImageOverlay& overlay) { return m_impl->add_overlay(id, overlay); }

media_library_return Blender::add_overlay(const std::string& id, const TextOverlay& overlay) { return m_impl->add_overlay(id, overlay); }

media_library_return Blender::add_overlay(const std::string& id, const DateTimeOverlay& overlay) { return m_impl->add_overlay(id, overlay); }

media_library_return Blender::add_overlay(const std::string& id, const CustomOverlay& overlay) { return m_impl->add_overlay(id, overlay); }

tl::expected<std::shared_ptr<Overlay>, media_library_return> Blender::get_overlay(const std::string& id) { return m_impl->get_overlay(id); }

media_library_return Blender::set_overlay(const std::string& id, const ImageOverlay& overlay) { return m_impl->set_overlay(id, overlay); }

media_library_return Blender::set_overlay(const std::string& id, const TextOverlay& overlay) { return m_impl->set_overlay(id, overlay); }

media_library_return Blender::set_overlay(const std::string& id, const DateTimeOverlay& overlay) { return m_impl->set_overlay(id, overlay); }

media_library_return Blender::set_overlay(const std::string& id, const CustomOverlay& overlay) { return m_impl->set_overlay(id, overlay); }

media_library_return Blender::remove_overlay(const std::string& id) { return m_impl->remove_overlay(id); }

media_library_return Blender::blend(dsp_image_properties_t& input_image_properties) { return m_impl->blend(input_image_properties); }

media_library_return Blender::set_frame_size(int frame_width, int frame_height) { return m_impl->set_frame_size(frame_width, frame_height); }

}