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
    enum rotation_alignment_policy_t
    {
        CENTER,
        TOP_LEFT
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

        Overlay() = default;
        Overlay(std::string id, float x, float y, unsigned int z_index, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
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
        ImageOverlay(std::string id, float x, float y, float width, float height, std::string image_path, unsigned int z_index, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
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

        /** Font size */
        float font_size;

        /** line thickness */
        int line_thickness;

        /** Path to load font from. The font will be scaled to match the given font_size */
        std::string font_path;

        /** Color of the text shadow. Shadow is disabled if any of the color components are negative */
        rgba_color_t shadow_color;

        /** Horizontal offset of the shadow relative to the text, in frame width ratio */
        float shadow_offset_x;

        /** Vertical offset of the shadow relative to the text, in frame height ratio */
        float shadow_offset_y;

        BaseTextOverlay();
        BaseTextOverlay(std::string id, float x, float y, std::string label, rgba_color_t text_color, rgba_color_t background_color, float font_size, int line_thickness,
                        unsigned int z_index, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
        BaseTextOverlay(std::string id, float x, float y, std::string label, rgba_color_t text_color, rgba_color_t background_color, float font_size, int line_thickness,
                        unsigned int z_index, std::string font_path, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
        BaseTextOverlay(std::string id, float x, float y, std::string label, rgba_color_t text_color, rgba_color_t background_color, float font_size, int line_thickness,
                        unsigned int z_index, std::string font_path, unsigned int angle, rotation_alignment_policy_t _rotation_policy,
                        rgba_color_t shadow_color, float shadow_offset_x, float shadow_offset_y);
    };

    struct TextOverlay : BaseTextOverlay
    {
        TextOverlay();
        TextOverlay(std::string id, float x, float y, std::string label, rgba_color_t text_color, rgba_color_t background_color, float font_size,
                    int line_thickness, unsigned int z_index, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
        TextOverlay(std::string id, float x, float y, std::string label, rgba_color_t text_color, rgba_color_t background_color, float font_size,
                    int line_thickness, unsigned int z_index, std::string font_path, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
        TextOverlay(std::string id, float x, float y, std::string label, rgba_color_t text_color, rgba_color_t background_color, float font_size,
                    int line_thickness, unsigned int z_index, std::string font_path, unsigned int angle, rotation_alignment_policy_t _rotation_policy,
                    rgba_color_t shadow_color, float shadow_offset_x, float shadow_offset_y);
    };

    /**
     * Overlay containing an auto-updating timestamp
     * @note timestamp is updated once per second
     */
    struct DateTimeOverlay : BaseTextOverlay
    {
        /**
         * format string for the datetime overlay. default is %d-%m-%Y %H:%M:%S", meaning "day-month-year hour:minute:second"
         */
        std::string datetime_format;

        DateTimeOverlay();
        DateTimeOverlay(std::string _id, float _x, float _y, std::string datetime_format, rgba_color_t _text_color, rgba_color_t _background_color, std::string font_path,
                        float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
        DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, rgba_color_t _background_color, std::string font_path, float _font_size,
                        int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
        DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, rgba_color_t _background_color, float _font_size, int _line_thickness,
                        unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
        DateTimeOverlay(std::string _id, float _x, float _y, rgba_color_t _text_color, float _font_size, int _line_thickness, unsigned int _z_index,
                        unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
        DateTimeOverlay(std::string _id, float _x, float _y, std::string datetime_format, rgba_color_t _text_color, rgba_color_t _background_color, std::string font_path,
                        float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy,
                        rgba_color_t shadow_color, float shadow_offset_x, float shadow_offset_y);
    };

    /** Overlay containing custom buffer ptr */
    struct CustomOverlay : Overlay
    {
        float width;
        float height;

        CustomOverlay() = default;
        CustomOverlay(std::string id, float x, float y, float width, float height, DspImagePropertiesPtr buffer, unsigned int z_index); // this is only in use for the get_metadata
        CustomOverlay(std::string id, float x, float y, float width, float height, unsigned int z_index, custom_overlay_format format);
        custom_overlay_format get_format() const { return m_format; }
        DspImagePropertiesPtr get_buffer() const { return m_buffer; }

    private:
        custom_overlay_format m_format;
        DspImagePropertiesPtr m_buffer;
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
        std::shared_future<tl::expected<std::shared_ptr<Blender>, media_library_return>> create_async(const std::string &config);

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
         * @note You must specify all the traits in the :overlay param -
         *       fields which aree to remain unchanged must also be filled
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
        media_library_return blend(dsp_image_properties_t &input_image_properties);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;

    public:
        Blender(std::unique_ptr<Impl> impl);
        ~Blender();
    };

}