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
 * @file osd.hpp
 * @brief OSD (On Screen Display) CPP API module
 **/
#pragma once

#include "media_library/buffer_pool.hpp"
#include "media_library/dsp_utils.hpp"
#include "media_library/media_library_types.hpp"
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <tl/expected.hpp>

#define DEFAULT_FONT_PATH "/usr/share/fonts/ttf/LiberationMono-Regular.ttf"
#define DEFAULT_DATETIME_STRING "%d-%m-%Y %H:%M:%S"

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

struct rgb_color_t
{
    int red;
    int green;
    int blue;
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
     * A420 Format - planar 4:4:2:0 AYUV. Each component is 8bit \n
     * For A420 format, the dimensions of the image, both width and height, need to be even numbers \n
     * Four planes in the following order: Y plane, U plane, V plane, Alpha plane
     */
    A420,

    /**
     * ARGB - RGB with alpha channel first (packed) format. One plane, each color component is 8bit \n
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

/** Overlay base strcut */
struct Overlay
{
    /**
     * Unique string identifier for the overlay
     * This id is used for all future operations on the overlay
     */
    std::string id;
    /**
     * Horizontal position in frame.
     * Position is relative and denoted with a decimal number between [0, 1]
     */
    float x;
    /**
     * Vertical position in frame.
     * Position is relative and denoted with a decimal number between [0, 1]
     */
    float y;
    /**
     * Blend order when overlays overlay.
     * Overlays with higher :z_index value are blender on top of overlays with lower :z_index value
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
};

/** Overlay containing an image (from a file) */
struct ImageOverlay : Overlay
{
    /**
     * Width is relative and denoted with a decimal number between [0, 1]
     */
    float width;
    /**
     * Height is relative and denoted with a decimal number between [0, 1]
     */
    float height;
    /**
     * Path to load image from. The image will be scaled to match the given :width and :height
     */
    std::string image_path;

    ImageOverlay() = default;
    ImageOverlay(std::string _id, float _x, float _y, float _width, float _height, std::string _image_path,
                 unsigned int _z_index, unsigned int _angle = 0,
                 rotation_alignment_policy_t _rotation_policy = rotation_alignment_policy_t::CENTER,
                 HorizontalAlignment _horizontal_alignment = HorizontalAlignment::LEFT,
                 VerticalAlignment _vertical_alignment = VerticalAlignment::TOP);
};

/** Overlay containing text */
struct BaseTextOverlay : Overlay
{
    /** Text content */
    std::string label;

    /** Foreground Text color */
    rgba_color_t text_color;

    /** Background color */
    rgba_color_t background_color;

    /** Path to load font from. The font will be scaled to match the given font_size */
    std::string font_path;

    /** Font size */
    float font_size;

    /** Line thickness */
    int line_thickness;

    /** Color of the text shadow. Shadow is disabled if any of the color components are negative */
    rgba_color_t shadow_color;

    /** Horizontal offset of the shadow relative to the text, in frame width ratio */
    float shadow_offset_x;

    /** Vertical offset of the shadow relative to the text, in frame height ratio */
    float shadow_offset_y;

    /** Either normal or bold */
    font_weight_t font_weight;

    /** Outline size */
    int outline_size;

    /** Outline Text color */
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

/**
 * @}
 *
 * @brief Overlay manager
 * @details Support the addition, removal and modification of overlays
 *          Overlay traits are defined using the structs above
 */
class Blender : public std::enable_shared_from_this<Blender>
{
  public:
    static tl::expected<std::shared_ptr<Blender>, media_library_return> create();
    static tl::expected<std::shared_ptr<Blender>, media_library_return> create(const std::string &config);
    std::shared_future<tl::expected<std::shared_ptr<Blender>, media_library_return>> create_async();
    std::shared_future<tl::expected<std::shared_ptr<Blender>, media_library_return>> create_async(
        const std::string &config);

    /**
     * @brief Add a new overlay
     * @details The new overlay will be blended upon each subsequent call to :blend
     * @param[in] id Unique string identifier for the overlay
     *               This id is used for all future operations on the overlay
     * @param[in] overlay Overlay to add
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     */
    media_library_return add_overlay(const ImageOverlay &overlay);
    media_library_return add_overlay(const TextOverlay &overlay);
    media_library_return add_overlay(const DateTimeOverlay &overlay);
    media_library_return add_overlay(const CustomOverlay &overlay);
    media_library_return set_overlay_enabled(const std::string &id, bool enabled);

    std::shared_future<media_library_return> add_overlay_async(const ImageOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const TextOverlay &overlay);
    std::shared_future<media_library_return> add_overlay_async(const DateTimeOverlay &overlay);

    /**
     * @brief Retrieve info of an existing overlay.
     * @param[in] id String identifier of the overlay to get.
     * @return :shared_ptr holding the overlay info if successfull
     *         otherwise a :media_library_return error
     * @note The return overlay should be downcast to the correct overlay type using :std::static_pointer_cast
     *       The caller of the function is responsible to know which overlay type is denoted by each id
     *       Casting to an incorrect type might result in undefined behaviour
     */
    tl::expected<std::shared_ptr<Overlay>, media_library_return> get_overlay(const std::string &id);

    /**
     * @brief Modify an existing overlay.
     * @param[in] id String identifier of the overlay to modify.
     * @param[in] overlay Overlay traits to modify
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     * @note All the traits in the :overlay param must be specified -
     *       that this fields which agree to remain unchanged must also be filled
     *       Use :get_overlay function to the current overlay state and perform changes
     */
    media_library_return set_overlay(const ImageOverlay &overlay);
    media_library_return set_overlay(const TextOverlay &overlay);
    media_library_return set_overlay(const DateTimeOverlay &overlay);
    media_library_return set_overlay(const CustomOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const ImageOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const TextOverlay &overlay);
    std::shared_future<media_library_return> set_overlay_async(const DateTimeOverlay &overlay);

    media_library_return configure(const std::string &config);
    /**
     * @brief Remove an exiting overlay
     * @details Subsequent calls to :blend will no longer blend the overlay
     * @param[in] id String identifier of the overlay to remove
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     */
    media_library_return remove_overlay(const std::string &id);
    std::shared_future<media_library_return> remove_overlay_async(const std::string &id);

    media_library_return set_frame_size(int frame_width, int frame_height);
    media_library_return blend(HailoMediaLibraryBufferPtr &input_buffer);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

  public:
    Blender(std::unique_ptr<Impl> impl);
    ~Blender();
};

} // namespace osd
