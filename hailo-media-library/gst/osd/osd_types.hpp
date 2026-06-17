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

/**
 * @file osd_types.hpp
 * @brief OSD (On Screen Display) CPP API module
 **/
#pragma once

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <hailo/hailodsp_base.h>
#include <stddef.h>
#include <string>

#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"

#define DEFAULT_FONT_PATH "/usr/share/fonts/ttf/LiberationMono-Regular.ttf"
#define DEFAULT_DATETIME_STRING "%d-%m-%Y %H:%M:%S"

#define OSD_SECTION_IMAGE "image"
#define OSD_SECTION_TEXT "text"
#define OSD_SECTION_DATETIME "dateTime"
#define OSD_SECTION_CUSTOM "custom"
#define OSD_ALL_SECTIONS {OSD_SECTION_IMAGE, OSD_SECTION_TEXT, OSD_SECTION_DATETIME, OSD_SECTION_CUSTOM}

namespace osd
{

class HorizontalAlignment
{
  public:
    static const HorizontalAlignment LEFT;
    static const HorizontalAlignment CENTER;
    static const HorizontalAlignment RIGHT;
    HorizontalAlignment() = default;
    HorizontalAlignment(HorizontalAlignment const &other) = default;
    HorizontalAlignment &operator=(const HorizontalAlignment &other) = default;
    static tl::expected<HorizontalAlignment, media_library_return> create(float alignment);
    float as_float() const
    {
        return m_alignment;
    }

    bool operator==(const HorizontalAlignment &other) const
    {
        return as_float() == other.as_float();
    }

  private:
    HorizontalAlignment(float alignment) : m_alignment(alignment)
    {
    }
    float m_alignment;
};

class VerticalAlignment
{
  public:
    static const VerticalAlignment TOP;
    static const VerticalAlignment CENTER;
    static const VerticalAlignment BOTTOM;
    VerticalAlignment() = default;
    VerticalAlignment(VerticalAlignment const &other) = default;
    VerticalAlignment &operator=(const VerticalAlignment &other) = default;
    static tl::expected<VerticalAlignment, media_library_return> create(float alignment);
    float as_float() const
    {
        return m_alignment;
    }

    bool operator==(const VerticalAlignment &other) const
    {
        return as_float() == other.as_float();
    }

  private:
    VerticalAlignment(float alignment) : m_alignment(alignment)
    {
    }
    float m_alignment;
};

enum rotation_alignment_policy_t
{
    CENTER,
    TOP_LEFT
};

enum font_weight_t
{
    NORMAL,
    BOLD
};

struct rgba_color_t
{
    int red;
    int green;
    int blue;
    int alpha;
};

typedef enum
{
    /**
     * A420 Format - planar 4:4:2:0 AYUV. Each component is 8-bit. \n
     * For A420 format, the dimensions of the image, both width and height, need to be even numbers \n
     * Four planes in the following order: Y plane, U plane, V plane, Alpha plane.
     */
    A420,

    /**
     * ARGB - RGB with alpha channel first (packed) format. One plane, each color component is 8-bit. \n
     * @code
     * +--+--+--+--+ +--+--+--+--+
     * |A0|R0|G0|B0| |A1|R1|G1|B1| ...
     * +--+--+--+--+ +--+--+--+--+
     * @endcode
     */
    ARGB,

    /* Must be last */
    COUNT,
    /** Max enum value to maintain ABI Integrity */
    ENUM = DSP_MAX_ENUM
} custom_overlay_format;

mat_dims calculate_text_size(const std::string &label, const std::string &font_path, int font_size, int line_thickness);

/**
 * @defgroup overlays Overlay structs
 * @{
 */

/** Overlay base struct */
struct Overlay
{
    /**
     * Unique string identifier for the overlay.
     * This id is used for all future operations on the overlay.
     */
    std::string id;
    /**
     * Horizontal position in frame.
     * Position is relative and denoted with a decimal number between [0, 1].
     */
    float x;
    /**
     * Vertical position in frame.
     * Position is relative and denoted with a decimal number between [0, 1].
     */
    float y;
    /**
     * Blend order when overlays overlay.
     * Overlays with higher :z_index value are blender on top of overlays with lower :z_index value.
     */
    unsigned int z_index;
    /**
     * Blend angle to rotate the overlay.
     */
    unsigned int angle;
    /**
     * Rotation alignment policy.
     * Defines the rotation center of the overlay.
     */
    rotation_alignment_policy_t rotation_alignment_policy;

    /**
     * Horizontal alignment of the overlay.
     * Defines the horizontal alignment of the overlay relative to the x position.
     */
    HorizontalAlignment horizontal_alignment;

    /**
     * Vertical alignment of the overlay.
     * Defines the vertical alignment of the overlay relative to the y position.
     */
    VerticalAlignment vertical_alignment;

    Overlay() = default;
    Overlay(std::string _id, float _x, float _y, unsigned int _z_index, unsigned int _angle = 0,
            rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
            HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
            VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);

    bool operator==(const Overlay &other) const
    {
        return (id == other.id) && (x == other.x) && (y == other.y) && (z_index == other.z_index) &&
               (angle == other.angle) && (rotation_alignment_policy == other.rotation_alignment_policy) &&
               (horizontal_alignment == other.horizontal_alignment) && (vertical_alignment == other.vertical_alignment);
    }

    bool operator!=(const Overlay &other) const
    {
        return !(*this == other);
    }
};

/** Overlay containing an image (from a file) */
struct ImageOverlay : Overlay
{
    /**
     * Width is relative and denoted with a decimal number between [0, 1].
     */
    float width;
    /**
     * Height is relative and denoted with a decimal number between [0, 1].
     */
    float height;
    /**
     * Path to load image from. The image will be scaled to match the given :width and :height.
     */
    std::string image_path;

    ImageOverlay() = default;
    ImageOverlay(std::string _id, float _x, float _y, float _width, float _height, std::string _image_path,
                 unsigned int _z_index, unsigned int _angle = 0,
                 rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
                 HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
                 VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);

    bool operator==(const ImageOverlay &other) const
    {
        return Overlay::operator==(other) && (width == other.width) && (height == other.height) &&
               (image_path == other.image_path);
    }

    bool operator!=(const ImageOverlay &other) const
    {
        return !(*this == other);
    }
};

/** Overlay containing text */
struct BaseTextOverlay : Overlay
{
    /** Text content. */
    std::string label;

    /** Foreground Text color. */
    rgba_color_t text_color;

    /** Background color. */
    rgba_color_t background_color;

    /** Path to load font from. The font will be scaled to match the given font_size. */
    std::string font_path;

    /** Font size. */
    float font_size;

    /** Line thickness. */
    int line_thickness;

    /** Color of the text shadow. Shadow is disabled if any of the color components are negative. */
    rgba_color_t shadow_color;

    /** Horizontal offset of the shadow relative to the text, in frame width ratio. */
    float shadow_offset_x;

    /** Vertical offset of the shadow relative to the text, in frame height ratio. */
    float shadow_offset_y;

    /** Either normal or bold. */
    font_weight_t font_weight;

    /** Outline size. */
    int outline_size;

    /** Outline Text color. */
    rgba_color_t outline_color;

    BaseTextOverlay();
    BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                    rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                    unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
    BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                    rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                    std::string _font_path, unsigned int _angle = 0,
                    rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
                    rgba_color_t _shadow_color = {-1, -1, -1, -1}, float _shadow_offset_x = 0,
                    float _shadow_offset_y = 0, font_weight_t _font_weight = font_weight_t::NORMAL,
                    int _outline_size = 0, rgba_color_t _outline_color = {-1, -1, -1, -1},
                    HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
                    VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);

    // this is only used internally in get_metadata
    BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                    rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                    std::string _font_path, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                    rgba_color_t shadow_color, float _shadow_offset_x, float _shadow_offset_y,
                    font_weight_t _font_weight, int _outline_size, rgba_color_t _outline_color,
                    HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment, size_t width,
                    size_t height);

    size_t get_width() const
    {
        return m_width;
    }
    size_t get_height() const
    {
        return m_height;
    }

    bool operator==(const BaseTextOverlay &other) const
    {
        return Overlay::operator==(other) && (text_color.red == other.text_color.red) &&
               (text_color.green == other.text_color.green) && (text_color.blue == other.text_color.blue) &&
               (text_color.alpha == other.text_color.alpha) && (background_color.red == other.background_color.red) &&
               (background_color.green == other.background_color.green) &&
               (background_color.blue == other.background_color.blue) &&
               (background_color.alpha == other.background_color.alpha) && (font_path == other.font_path) &&
               (font_size == other.font_size) && (line_thickness == other.line_thickness) &&
               (shadow_color.red == other.shadow_color.red) && (shadow_color.green == other.shadow_color.green) &&
               (shadow_color.blue == other.shadow_color.blue) && (shadow_color.alpha == other.shadow_color.alpha) &&
               (shadow_offset_x == other.shadow_offset_x) && (shadow_offset_y == other.shadow_offset_y) &&
               (font_weight == other.font_weight) && (outline_size == other.outline_size) &&
               (outline_color.red == other.outline_color.red) && (outline_color.green == other.outline_color.green) &&
               (outline_color.blue == other.outline_color.blue) && (outline_color.alpha == other.outline_color.alpha);
    }

  private:
    size_t m_width;
    size_t m_height;
};

struct TextOverlay : BaseTextOverlay
{
    TextOverlay();
    TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
    TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                std::string _font_path, unsigned int _angle = 0,
                rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
                rgba_color_t _shadow_color = {-1, -1, -1, -1}, float _shadow_offset_x = 0, float _shadow_offset_y = 0,
                font_weight_t _font_weight = font_weight_t::NORMAL, int _outline_size = 0,
                rgba_color_t _outline_color = {-1, -1, -1, -1},
                HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
                VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);

    // this is only used internally in get_metadata
    TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                rgba_color_t _background_color, float _font_size, int _line_thickness, unsigned int _z_index,
                std::string _font_path, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                rgba_color_t _shadow_color, float _shadow_offset_x, float _shadow_offset_y, font_weight_t _font_weight,
                int _outline_size, rgba_color_t _outline_color, HorizontalAlignment _horizontal_alignment,
                VerticalAlignment _vertical_alignment, size_t width, size_t height);

    bool operator==(const TextOverlay &other) const
    {
        return BaseTextOverlay::operator==(other) && (label == other.label);
    }

    bool operator!=(const TextOverlay &other) const
    {
        return !(*this == other);
    }
};

/**
 * Overlay containing an auto-updating timestamp
 * @note The timestamp is updated once per second
 */
struct DateTimeOverlay : BaseTextOverlay
{
    /**
     * format string for the datetime overlay. default is %d-%m-%Y %H:%M:%S", meaning "day-month-year
     * hour:minute:second"
     */
    std::string datetime_format;

    DateTimeOverlay();
    DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format, rgba_color_t _text_color,
                    rgba_color_t _background_color, std::string _font_path, float _font_size, int _line_thickness,
                    unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
    DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, rgba_color_t _background_color,
                    std::string _font_path, float _font_size, int _line_thickness, unsigned int _z_index,
                    unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
    DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, rgba_color_t _background_color,
                    float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle,
                    rotation_alignment_policy_t _rotation_policy);
    DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, float _font_size,
                    int _line_thickness, unsigned int _z_index, unsigned int _angle,
                    rotation_alignment_policy_t _rotation_policy);
    DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format, rgba_color_t _text_color,
                    rgba_color_t _background_color, std::string _font_path, float _font_size, int _line_thickness,
                    unsigned int _z_index, unsigned int _angle = 0,
                    rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
                    rgba_color_t _shadow_color = {-1, -1, -1, -1}, float _shadow_offset_x = 0,
                    float _shadow_offset_y = 0, font_weight_t _font_weight = font_weight_t::NORMAL,
                    int _outline_size = 0, rgba_color_t _outline_color = {-1, -1, -1, -1},
                    HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
                    VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);

    // this is only used internally in get_metadata
    DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format, rgba_color_t _text_color,
                    rgba_color_t _background_color, std::string _font_path, float _font_size, int _line_thickness,
                    unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                    rgba_color_t _shadow_color, float _shadow_offset_x, float _shadow_offset_y,
                    font_weight_t _font_weight, int _outline_size, rgba_color_t _outline_color,
                    HorizontalAlignment _horizontal_alignment, VerticalAlignment _vertical_alignment, size_t width,
                    size_t height);

    bool operator==(const DateTimeOverlay &other) const
    {
        return BaseTextOverlay::operator==(other) && (datetime_format == other.datetime_format);
    }

    bool operator!=(const DateTimeOverlay &other) const
    {
        return !(*this == other);
    }
};

/** Overlay containing custom buffer ptr */
struct CustomOverlay : Overlay
{
    float width;
    float height;

    CustomOverlay() = default;
    CustomOverlay(std::string _id, float _x, float _y, float _width, float _height, unsigned int _z_index,
                  custom_overlay_format _format, unsigned int _angle = 0,
                  rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
                  HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
                  VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);

    // this is only used internally in get_metadata
    CustomOverlay(std::string _id, float _x, float _y, unsigned int _z_index, unsigned int _angle,
                  rotation_alignment_policy_t _rotation_policy, HorizontalAlignment _horizontal_alignment,
                  VerticalAlignment _vertical_alignment, float _width, float _height, custom_overlay_format _format,
                  HailoMediaLibraryBufferPtr _medialib_buffer);

    custom_overlay_format get_format() const
    {
        return m_format;
    }
    HailoMediaLibraryBufferPtr get_buffer() const
    {
        return m_medialib_buffer;
    }

  private:
    custom_overlay_format m_format;
    HailoMediaLibraryBufferPtr m_medialib_buffer;
};

/**
 * Structs above may be loaded from JSON
 * See https://json.nlohmann.me/features/arbitrary_types/
 */
void from_json(const nlohmann::json &json, ImageOverlay &overlay);
void from_json(const nlohmann::json &json, TextOverlay &overlay);
void from_json(const nlohmann::json &json, DateTimeOverlay &overlay);
void from_json(const nlohmann::json &json, CustomOverlay &overlay);

void to_json(nlohmann::json &json, const ImageOverlay &overlay);
void to_json(nlohmann::json &json, const TextOverlay &overlay);
void to_json(nlohmann::json &json, const DateTimeOverlay &overlay);
void to_json(nlohmann::json &json, const CustomOverlay &overlay);

} // namespace osd
