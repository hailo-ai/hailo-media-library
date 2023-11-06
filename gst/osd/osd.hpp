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

/**
 * @file osd.hpp
 * @brief OSD (On Screen Display) CPP API module
 **/
#pragma once

#include <string>
#include <memory>
#include <tl/expected.hpp>
#include <nlohmann/json.hpp>
#include "media_library/dsp_utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/buffer_pool.hpp"

#define DEFAULT_FONT_PATH "/usr/share/fonts/ttf/LiberationMono-Regular.ttf"

namespace osd {

struct RGBColor {
    int red;
    int green;
    int blue;
};

/** 
 * @defgroup overlays Overlay structs
 * @{
 */

/** Overlay base strcut */
struct Overlay {
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

    Overlay() = default;
    Overlay(float x, float y, unsigned int z_index);
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
    ImageOverlay(float x, float y, float width, float height, std::string image_path, unsigned int z_index);
};

/** Overlay containing text */
struct TextOverlay : Overlay
{
    /** Text content */
    std::string label;
    /** Text color */
    RGBColor rgb;
    float font_size;
    int line_thickness;
    std::string font_path;


    TextOverlay();
    TextOverlay(float x, float y, std::string label, RGBColor rgb, float font_size, int line_thickness, unsigned int z_index);
    TextOverlay(float x, float y, std::string label, RGBColor rgb, float font_size, int line_thickness, unsigned int z_index, std::string font_path);
};

/** 
 * Overlay containing an auto-updating timestamp
 * @note timestamp is updated once per second
 */
struct DateTimeOverlay : Overlay
{
    /** Text color */
    RGBColor rgb;
    float font_size;
    int line_thickness;
    std::string font_path;

    DateTimeOverlay();
    DateTimeOverlay(float x, float y, RGBColor rgb, float font_size, int line_thickness, unsigned int z_index);
    DateTimeOverlay(float x, float y, RGBColor rgb, float font_size, int line_thickness, unsigned int z_index,std::string font_path);
};

/** Overlay containing custom buffer ptr */
struct CustomOverlay : Overlay
{
    float width;
    float height;

    DspImagePropertiesPtr get_buffer() const { return m_buffer; }
    CustomOverlay() = default;
    CustomOverlay(float x, float y, float width, float height, DspImagePropertiesPtr buffer, unsigned int z_index);

    private:
        DspImagePropertiesPtr m_buffer;
};

/** 
 * Structs above may be loaded from JSON
 * See https://json.nlohmann.me/features/arbitrary_types/
 */
void from_json(const nlohmann::json& json, ImageOverlay& overlay);
void from_json(const nlohmann::json& json, TextOverlay& overlay);
void from_json(const nlohmann::json& json, DateTimeOverlay& overlay);
void from_json(const nlohmann::json& json, CustomOverlay& overlay);

/** 
 * @} 
 * 
 * @brief Overlay manager
 * @details Support the addition, removal and modification of overlays
 *          Overlay traits are defined using the structs above
 */
class Blender
{
public:
    static tl::expected<std::shared_ptr<Blender>, media_library_return> create();
    static tl::expected<std::shared_ptr<Blender>, media_library_return> create(const nlohmann::json& config);
  
    /**
     * @brief Add a new overlay
     * @details The new overlay will be blended upon each subsequent call to :blend
     * @param[in] id Unique string identifier for the overlay
     *               This id is used for all future operations on the overlay
     * @param[in] overlay Overlay to add
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
    */
    media_library_return add_overlay(const std::string& id, const ImageOverlay& overlay);
    media_library_return add_overlay(const std::string& id, const TextOverlay& overlay);
    media_library_return add_overlay(const std::string& id, const DateTimeOverlay& overlay);
    media_library_return add_overlay(const std::string& id, const CustomOverlay& overlay);

    /**
     * @brief Retrieve info of an existing overlay.
     * @param[in] id String identifier of the overlay to get.
     * @return :shared_ptr holding the overlay info if successfull
     *         otherwise a :media_library_return error
     * @note The return overlay should be downcast to the correct overlay type using :std::static_pointer_cast
     *       The caller of the function is responsible to know which overlay type is denoted by each id
     *       Casting to an incorrect type might result in undefined behaviour
     */
    tl::expected<std::shared_ptr<Overlay>, media_library_return> get_overlay(const std::string& id);

    /**
     * @brief Modify an existing overlay.
     * @param[in] id String identifier of the overlay to modify.
     * @param[in] overlay Overlay traits to modify
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
     * @note You must specify all the traits in the :overlay param -
     *       fields which aree to remain unchanged must also be filled
     *       Use :get_overlay function to the current overlay state and perform changes
    */
    media_library_return set_overlay(const std::string& id, const ImageOverlay& overlay);
    media_library_return set_overlay(const std::string& id, const TextOverlay& overlay);
    media_library_return set_overlay(const std::string& id, const DateTimeOverlay& overlay);
    media_library_return set_overlay(const std::string& id, const CustomOverlay& overlay);

    /**
     * @brief Remove an exiting overlay
     * @details Subsequent calls to :blend will no longer blend the overlay
     * @param[in] id String identifier of the overlay to remove
     * @return :MEDIA_LIBRARY_SUCCESS if successful, otherwise a :media_library_return error
    */
    media_library_return remove_overlay(const std::string& id);

    media_library_return set_frame_size(int frame_width, int frame_height);
    media_library_return blend(dsp_image_properties_t& input_image_properties);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  
public:
    Blender(std::unique_ptr<Impl> impl);
    ~Blender();
};

}
