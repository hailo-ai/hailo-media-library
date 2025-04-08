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

#include "impl/blender_impl.hpp"
#include "impl/custom_overlay_impl.hpp"
#include "impl/datetime_overlay_impl.hpp"
#include "impl/image_overlay_impl.hpp"
#include "impl/overlay_impl.hpp"
#include "impl/text_overlay_impl.hpp"
#include "media_library/media_library_logger.hpp"

#include <algorithm>

#define MODULE_NAME LoggerType::Osd

namespace osd
{
const auto DEFAULT_OSD_CONFIG = R"({
        "osd" : {
            "dateTime" : [
                {
                    "id" : "default_datetime",
                    "font_size" : 70,
                    "font_path" : "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                    "text_color" : [ 255, 255, 255 ],
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
                    "text_color" : [ 255, 255, 255 ],
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
                    "text_color" : [ 255, 255, 255 ],
                    "x" : 0.1,
                    "y" : 0.1,
                    "z-index" : 1,
                    "angle": 0,
                    "rotation_policy": "CENTER"
                }
            ]
        }
    })";

Overlay::Overlay(std::string _id, float _x, float _y, unsigned int _z_index, unsigned int _angle,
                 rotation_alignment_policy_t _rotation_policy, HorizontalAlignment _horizontal_alignment,
                 VerticalAlignment _vertical_alignment)
    : id(_id), x(_x), y(_y), z_index(_z_index), angle(_angle), rotation_alignment_policy(_rotation_policy),
      horizontal_alignment(_horizontal_alignment), vertical_alignment(_vertical_alignment)
{
}

ImageOverlay::ImageOverlay(std::string _id, float _x, float _y, float _width, float _height, std::string _image_path,
                           unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                           HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment)
    : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy, _horizontal_alignment, _vertical_alignment),
      width(_width), height(_height), image_path(_image_path)
{
}

BaseTextOverlay::BaseTextOverlay()
    : Overlay("", 0, 0, 1, 0, rotation_alignment_policy_t::CENTER), label(""), text_color(rgba_color_t()),
      background_color({-1, -1, -1, -1}), font_path(DEFAULT_FONT_PATH), font_size(20), line_thickness(1),
      shadow_color({-1, -1, -1, -1}), shadow_offset_x(0), shadow_offset_y(0), font_weight(font_weight_t::NORMAL),
      outline_size(0), outline_color({-1, -1, -1, -1}), m_width(0), m_height(0)
{
}

BaseTextOverlay::BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                                 rgba_color_t _background_color, float _font_size, int _line_thickness,
                                 unsigned int _z_index, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy)
    : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy), label(_label), text_color(_text_color),
      background_color(_background_color), font_path(DEFAULT_FONT_PATH), font_size(_font_size),
      line_thickness(_line_thickness), shadow_color({-1, -1, -1, -1}), shadow_offset_x(0), shadow_offset_y(0),
      font_weight(font_weight_t::NORMAL), outline_size(0), outline_color({-1, -1, -1, -1}), m_width(0), m_height(0)
{
}

BaseTextOverlay::BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                                 rgba_color_t _background_color, float _font_size, int _line_thickness,
                                 unsigned int _z_index, std::string _font_path, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy, rgba_color_t _shadow_color,
                                 float _shadow_offset_x, float _shadow_offset_y, font_weight_t _font_weight,
                                 int _outline_size, rgba_color_t _outline_color,
                                 HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment)
    : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy, _horizontal_alignment, _vertical_alignment),
      label(_label), text_color(_text_color), background_color(_background_color), font_path(_font_path),
      font_size(_font_size), line_thickness(_line_thickness), shadow_color(_shadow_color),
      shadow_offset_x(_shadow_offset_x), shadow_offset_y(_shadow_offset_y), font_weight(_font_weight),
      outline_size(_outline_size), outline_color(_outline_color), m_width(0), m_height(0)
{
}

BaseTextOverlay::BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                                 rgba_color_t _background_color, float _font_size, int _line_thickness,
                                 unsigned int _z_index, std::string _font_path, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy, rgba_color_t _shadow_color,
                                 float _shadow_offset_x, float _shadow_offset_y, font_weight_t _font_weight,
                                 int _outline_size, rgba_color_t _outline_color,
                                 HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment,
                                 size_t _width, size_t _height)
    : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy, _horizontal_alignment, _vertical_alignment),
      label(_label), text_color(_text_color), background_color(_background_color), font_path(_font_path),
      font_size(_font_size), line_thickness(_line_thickness), shadow_color(_shadow_color),
      shadow_offset_x(_shadow_offset_x), shadow_offset_y(_shadow_offset_y), font_weight(_font_weight),
      outline_size(_outline_size), outline_color(_outline_color), m_width(_width), m_height(_height)
{
}

TextOverlay::TextOverlay() : BaseTextOverlay()
{
}

TextOverlay::TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                         rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                         unsigned int _angle, rotation_alignment_policy_t _rotation_policy)
    : BaseTextOverlay(_id, _x, _y, _label, _text_color, _background_color, _font_size, _line_thickness, _z_index,
                      _angle, _rotation_policy)
{
}

TextOverlay::TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                         rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                         std::string _font_path, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                         rgba_color_t _shadow_color, float _shadow_offset_x, float _shadow_offset_y,
                         font_weight_t _font_weight, int _outline_size, rgba_color_t _outline_color,
                         HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment)
    : BaseTextOverlay(_id, _x, _y, _label, _text_color, _background_color, _font_size, _line_thickness, _z_index,
                      _font_path, _angle, _rotation_policy, _shadow_color, _shadow_offset_x, _shadow_offset_y,
                      _font_weight, _outline_size, _outline_color, _horizontal_alignment, _vertical_alignment)
{
}

TextOverlay::TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                         rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                         std::string _font_path, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                         rgba_color_t _shadow_color, float _shadow_offset_x, float _shadow_offset_y,
                         font_weight_t _font_weight, int _outline_size, rgba_color_t _outline_color,
                         HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment,
                         size_t _width, size_t _height)
    : BaseTextOverlay(_id, _x, _y, _label, _text_color, _background_color, _font_size, _line_thickness, _z_index,
                      _font_path, _angle, _rotation_policy, _shadow_color, _shadow_offset_x, _shadow_offset_y,
                      _font_weight, _outline_size, _outline_color, _horizontal_alignment, _vertical_alignment, _width,
                      _height)
{
}

DateTimeOverlay::DateTimeOverlay() : BaseTextOverlay(), datetime_format(DEFAULT_DATETIME_STRING)
{
}

DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, float _font_size,
                                 int _line_thickness, unsigned int _z_index, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy)
    : BaseTextOverlay(_id, _x, _y, "", _text_color, {-1, -1, -1, -1}, _font_size, _line_thickness, _z_index, _angle,
                      _rotation_policy),
      datetime_format(DEFAULT_DATETIME_STRING)
{
}

DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color,
                                 rgba_color_t _background_color, std::string _font_path, float _font_size,
                                 int _line_thickness, unsigned int _z_index, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy)
    : BaseTextOverlay(_id, _x, _y, "", _text_color, _background_color, _font_size, _line_thickness, _z_index,
                      _font_path, _angle, _rotation_policy),
      datetime_format(DEFAULT_DATETIME_STRING)
{
}

DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format,
                                 rgba_color_t _text_color, rgba_color_t _background_color, std::string _font_path,
                                 float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy)
    : BaseTextOverlay(_id, _x, _y, "", _text_color, _background_color, _font_size, _line_thickness, _z_index,
                      _font_path, _angle, _rotation_policy),
      datetime_format(_datetime_format)
{
}

DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format,
                                 rgba_color_t _text_color, rgba_color_t _background_color, std::string _font_path,
                                 float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy, rgba_color_t _shadow_color,
                                 float _shadow_offset_x, float _shadow_offset_y, font_weight_t _font_weight,
                                 int _outline_size, rgba_color_t _outline_color,
                                 HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment)
    : BaseTextOverlay(_id, _x, _y, _datetime_format, _text_color, _background_color, _font_size, _line_thickness,
                      _z_index, _font_path, _angle, _rotation_policy, _shadow_color, _shadow_offset_x, _shadow_offset_y,
                      _font_weight, _outline_size, _outline_color, _horizontal_alignment, _vertical_alignment),
      datetime_format(_datetime_format)
{
}

DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format,
                                 rgba_color_t _text_color, rgba_color_t _background_color, std::string _font_path,
                                 float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle,
                                 rotation_alignment_policy_t _rotation_policy, rgba_color_t _shadow_color,
                                 float _shadow_offset_x, float _shadow_offset_y, font_weight_t _font_weight,
                                 int _outline_size, rgba_color_t _outline_color,
                                 HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment,
                                 size_t _width, size_t _height)
    : BaseTextOverlay(_id, _x, _y, _datetime_format, _text_color, _background_color, _font_size, _line_thickness,
                      _z_index, _font_path, _angle, _rotation_policy, _shadow_color, _shadow_offset_x, _shadow_offset_y,
                      _font_weight, _outline_size, _outline_color, _horizontal_alignment, _vertical_alignment, _width,
                      _height),
      datetime_format(_datetime_format)
{
}

CustomOverlay::CustomOverlay(std::string _id, float _x, float _y, unsigned int _z_index, unsigned int _angle,
                             rotation_alignment_policy_t _rotation_policy, HorizontalAlignment _horizontal_alignment,
                             VerticalAlignment _vertical_alignment, float _width, float _height,
                             custom_overlay_format _format, HailoMediaLibraryBufferPtr _medialib_buffer)
    : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy, _horizontal_alignment, _vertical_alignment),
      width(_width), height(_height), m_format(_format), m_medialib_buffer(_medialib_buffer)
{
}

CustomOverlay::CustomOverlay(std::string _id, float _x, float _y, float _width, float _height, unsigned int _z_index,
                             custom_overlay_format _format, unsigned int _angle,
                             rotation_alignment_policy_t _rotation_policy, HorizontalAlignment _horizontal_alignment,
                             VerticalAlignment _vertical_alignment)
    : Overlay(_id, _x, _y, _z_index, _angle, _rotation_policy, _horizontal_alignment, _vertical_alignment),
      width(_width), height(_height), m_format(_format)
{
}

template <typename BasicJsonType> void from_json(const BasicJsonType &j, rotation_alignment_policy_t &e)
{
    if (j == "CENTER")
    {
        e = rotation_alignment_policy_t::CENTER;
        return;
    }
    if (j == "TOP_LEFT")
    {
        e = rotation_alignment_policy_t::TOP_LEFT;
        return;
    }
    LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown enum value received for rotation_alignment_policy_t");
    throw std::invalid_argument("Unknown enum value received for rotation_alignment_policy_t");
}

template <typename BasicJsonType> inline void from_json(const BasicJsonType &j, font_weight_t &e)
{
    if (j == "NORMAL")
    {
        e = font_weight_t::NORMAL;
        return;
    }
    if (j == "BOLD")
    {
        e = font_weight_t::BOLD;
        return;
    }
    LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown enum value received for font_weight_t");
    throw std::invalid_argument("Unknown enum value received for font_weight_t");
}

void from_json(const nlohmann::json &json, rgba_color_t &text_color)
{
    json.at(0).get_to(text_color.red);
    json.at(1).get_to(text_color.green);
    json.at(2).get_to(text_color.blue);
    if (json.size() > 3)
    {
        json.at(3).get_to(text_color.alpha);
    }
    else
    {
        text_color.alpha = 255;
    }
}

void from_json(const nlohmann::json &json, HorizontalAlignment &alignment)
{
    if (json.is_string())
    {
        if (json == "LEFT")
        {
            alignment = HorizontalAlignment::LEFT;
        }
        else if (json == "CENTER")
        {
            alignment = HorizontalAlignment::CENTER;
        }
        else if (json == "RIGHT")
        {
            alignment = HorizontalAlignment::RIGHT;
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown horizontal alignment value received");
            throw std::invalid_argument("Unknown horizontal alignment value received");
        }
    }
    else
    {
        float value;
        json.get_to(value);
        auto alignment_expected = HorizontalAlignment::create(value);
        if (alignment_expected.has_value())
        {
            alignment = alignment_expected.value();
        }
        else
        {
            throw std::invalid_argument("Invalid horizontal alignment value received");
        }
    }
}

void from_json(const nlohmann::json &json, VerticalAlignment &alignment)
{
    if (json.is_string())
    {
        if (json == "TOP")
        {
            alignment = VerticalAlignment::TOP;
        }
        else if (json == "CENTER")
        {
            alignment = VerticalAlignment::CENTER;
        }
        else if (json == "BOTTOM")
        {
            alignment = VerticalAlignment::BOTTOM;
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown vertical alignment value received");
            throw std::invalid_argument("Unknown vertical alignment value received");
        }
    }
    else
    {
        float value;
        json.get_to(value);
        auto alignment_expected = VerticalAlignment::create(value);
        if (alignment_expected.has_value())
        {
            alignment = alignment_expected.value();
        }
        else
        {
            throw std::invalid_argument("Invalid vertical alignment value received");
        }
    }
}

template <typename T> void json_get_if_exists(const nlohmann::json &json, const std::string &key, T &overlay_member)
{
    // If the key exists in the json object, assign the value to the overlay member
    // Otherwise, keep the overlay member as is
    overlay_member = json.value(key, overlay_member);
}

template <typename T>
void json_get_if_exists(const nlohmann::json &json, const std::string &key, T &overlay_member, T default_value)
{
    // If the key exists in the json object, assign the value to the overlay member
    // Otherwise, set default value
    overlay_member = json.value(key, default_value);
}

static const VerticalAlignment default_vertical_alignment = VerticalAlignment::create(0.0).value();
static const HorizontalAlignment default_horizontal_alignment = HorizontalAlignment::create(0.0).value();

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
    json_get_if_exists(json, "horizontal_alignment", overlay.horizontal_alignment, default_horizontal_alignment);
    json_get_if_exists(json, "vertical_alignment", overlay.vertical_alignment, default_vertical_alignment);
}

void from_json(const nlohmann::json &json, TextOverlay &overlay)
{
    json.at("id").get_to(overlay.id);
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json.at("label").get_to(overlay.label);
    json.at("text_color").get_to(overlay.text_color);
    json_get_if_exists(json, "background_color", overlay.background_color);
    json.at("font_size").get_to(overlay.font_size);
    json_get_if_exists(json, "line_thickness", overlay.line_thickness);
    json_get_if_exists(json, "outline_size", overlay.outline_size);
    json_get_if_exists(json, "outline_color", overlay.outline_color);
    json.at("z-index").get_to(overlay.z_index);
    json.at("font_path").get_to(overlay.font_path);
    json.at("angle").get_to(overlay.angle);
    json.at("rotation_policy").get_to(overlay.rotation_alignment_policy);
    json_get_if_exists(json, "shadow_offset_x", overlay.shadow_offset_x);
    json_get_if_exists(json, "shadow_offset_y", overlay.shadow_offset_y);
    json_get_if_exists(json, "shadow_color", overlay.shadow_color);
    json_get_if_exists(json, "font_weight", overlay.font_weight);
    json_get_if_exists(json, "horizontal_alignment", overlay.horizontal_alignment, default_horizontal_alignment);
    json_get_if_exists(json, "vertical_alignment", overlay.vertical_alignment, default_vertical_alignment);
}

void from_json(const nlohmann::json &json, DateTimeOverlay &overlay)
{
    json.at("id").get_to(overlay.id);
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json_get_if_exists(json, "datetime_format", overlay.datetime_format);
    json.at("text_color").get_to(overlay.text_color);
    json_get_if_exists(json, "background_color", overlay.background_color);
    json.at("font_size").get_to(overlay.font_size);
    json.at("font_path").get_to(overlay.font_path);
    json_get_if_exists(json, "line_thickness", overlay.line_thickness);
    json_get_if_exists(json, "outline_size", overlay.outline_size);
    json_get_if_exists(json, "outline_color", overlay.outline_color);
    json.at("z-index").get_to(overlay.z_index);
    json.at("angle").get_to(overlay.angle);
    json.at("rotation_policy").get_to(overlay.rotation_alignment_policy);
    json_get_if_exists(json, "shadow_offset_x", overlay.shadow_offset_x);
    json_get_if_exists(json, "shadow_offset_y", overlay.shadow_offset_y);
    json_get_if_exists(json, "shadow_color", overlay.shadow_color);
    json_get_if_exists(json, "font_weight", overlay.font_weight);
    json_get_if_exists(json, "horizontal_alignment", overlay.horizontal_alignment, default_horizontal_alignment);
    json_get_if_exists(json, "vertical_alignment", overlay.vertical_alignment, default_vertical_alignment);
}

void from_json(const nlohmann::json &json, CustomOverlay &overlay)
{
    json.at("id").get_to(overlay.id);
    json.at("x").get_to(overlay.x);
    json.at("y").get_to(overlay.y);
    json.at("width").get_to(overlay.width);
    json.at("height").get_to(overlay.height);
    json.at("z-index").get_to(overlay.z_index);
    json_get_if_exists(json, "horizontal_alignment", overlay.horizontal_alignment, default_horizontal_alignment);
    json_get_if_exists(json, "vertical_alignment", overlay.vertical_alignment, default_vertical_alignment);
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

std::shared_future<tl::expected<std::shared_ptr<Blender>, media_library_return>> Blender::create_async(
    const std::string &config)
{
    return std::async(std::launch::async, [config]() { return create(config); }).share();
}

Blender::Blender(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}

Blender::~Blender() = default;

media_library_return Blender::add_overlay(const ImageOverlay &overlay)
{
    return m_impl->add_overlay(overlay);
}

media_library_return Blender::add_overlay(const TextOverlay &overlay)
{
    return m_impl->add_overlay(overlay);
}

media_library_return Blender::add_overlay(const DateTimeOverlay &overlay)
{
    return m_impl->add_overlay(overlay);
}

media_library_return Blender::add_overlay(const CustomOverlay &overlay)
{
    return m_impl->add_overlay(overlay);
}

media_library_return Blender::set_overlay_enabled(const std::string &id, bool enabled)
{
    return m_impl->set_overlay_enabled(id, enabled);
}

std::shared_future<media_library_return> Blender::add_overlay_async(const ImageOverlay &overlay)
{
    return m_impl->add_overlay_async(overlay);
}

std::shared_future<media_library_return> Blender::add_overlay_async(const TextOverlay &overlay)
{
    return m_impl->add_overlay_async(overlay);
}

std::shared_future<media_library_return> Blender::add_overlay_async(const DateTimeOverlay &overlay)
{
    return m_impl->add_overlay_async(overlay);
}

tl::expected<std::shared_ptr<Overlay>, media_library_return> Blender::get_overlay(const std::string &id)
{
    return m_impl->get_overlay(id);
}

media_library_return Blender::set_overlay(const ImageOverlay &overlay)
{
    return m_impl->set_overlay(overlay);
}

media_library_return Blender::set_overlay(const TextOverlay &overlay)
{
    return m_impl->set_overlay(overlay);
}

media_library_return Blender::set_overlay(const DateTimeOverlay &overlay)
{
    return m_impl->set_overlay(overlay);
}

media_library_return Blender::set_overlay(const CustomOverlay &overlay)
{
    return m_impl->set_overlay(overlay);
}

std::shared_future<media_library_return> Blender::set_overlay_async(const ImageOverlay &overlay)
{
    return m_impl->set_overlay_async(overlay);
}

std::shared_future<media_library_return> Blender::set_overlay_async(const TextOverlay &overlay)
{
    return m_impl->set_overlay_async(overlay);
}

std::shared_future<media_library_return> Blender::set_overlay_async(const DateTimeOverlay &overlay)
{
    return m_impl->set_overlay_async(overlay);
}

media_library_return Blender::remove_overlay(const std::string &id)
{
    return m_impl->remove_overlay(id);
}

std::shared_future<media_library_return> Blender::remove_overlay_async(const std::string &id)
{
    return m_impl->remove_overlay_async(id);
}

media_library_return Blender::blend(HailoMediaLibraryBufferPtr &input_buffer)
{
    return m_impl->blend(input_buffer);
}

media_library_return Blender::set_frame_size(int frame_width, int frame_height)
{
    return m_impl->set_frame_size(frame_width, frame_height);
}

mat_dims calculate_text_size(const std::string &label, const std::string &font_path, int font_size, int line_thickness)
{
    return internal_calculate_text_size(label, font_path, font_size, line_thickness);
}

media_library_return Blender::configure(const std::string &config)
{
    return m_impl->configure(config);
}
} // namespace osd
