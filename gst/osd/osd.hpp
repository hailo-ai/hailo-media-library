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
        /** Text color */
        rgb_color_t rgb;
        rgb_color_t rgb_background;

        /**
         * Font size
         */
        float font_size;
        /**
         * line thickness
         */
        int line_thickness;
        /**
         * Path to load font from. The font will be scaled to match the given font_size
         */
        std::string font_path;

        BaseTextOverlay();
        BaseTextOverlay(std::string id, float x, float y, std::string label, rgb_color_t rgb, rgb_color_t rgb_background, float font_size, int line_thickness, unsigned int z_index, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
        BaseTextOverlay(std::string id, float x, float y, std::string label, rgb_color_t rgb, rgb_color_t rgb_background, float font_size, int line_thickness, unsigned int z_index, std::string font_path, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
    };

    struct TextOverlay : BaseTextOverlay
    {
        TextOverlay();
        TextOverlay(std::string id, float x, float y, std::string label, rgb_color_t rgb, rgb_color_t rgb_background, float font_size, int line_thickness, unsigned int z_index, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
        TextOverlay(std::string id, float x, float y, std::string label, rgb_color_t rgb, rgb_color_t rgb_background, float font_size, int line_thickness, unsigned int z_index, std::string font_path, unsigned int angle, rotation_alignment_policy_t _rotation_policy);
    };

    /**
     * Overlay containing an auto-updating timestamp
     * @note timestamp is updated once per second
     */
    struct DateTimeOverlay : BaseTextOverlay
    {
        DateTimeOverlay();
        DateTimeOverlay(std::string _id, float _x, float _y, rgb_color_t _rgb, rgb_color_t _rgb_background, std::string font_path, float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
        DateTimeOverlay(std::string _id, float _x, float _y, rgb_color_t _rgb, rgb_color_t _rgb_background, float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
        DateTimeOverlay(std::string _id, float _x, float _y, rgb_color_t _rgb, float _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle, rotation_alignment_policy_t _rotation_policy);
    };

    /** Overlay containing custom buffer ptr */
    struct CustomOverlay : Overlay
    {
        float width;
        float height;

        DspImagePropertiesPtr get_buffer() const { return m_buffer; }
        CustomOverlay() = default;
        CustomOverlay(std::string id, float x, float y, float width, float height, DspImagePropertiesPtr buffer, unsigned int z_index);

    private:
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
        std::shared_future<media_library_return> add_overlay_async(const ImageOverlay &overlay);
        std::shared_future<media_library_return> add_overlay_async(const TextOverlay &overlay);
        std::shared_future<media_library_return> add_overlay_async(const DateTimeOverlay &overlay);
        std::shared_future<media_library_return> add_overlay_async(const CustomOverlay &overlay);

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
        std::shared_future<media_library_return> set_overlay_async(const CustomOverlay &overlay);

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
