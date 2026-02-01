#include "osd.hpp"
#include "media_library/media_library_logger.hpp"
#include "impl/overlay_impl.hpp"

#define MODULE_NAME LoggerType::Osd

namespace osd
{

mat_dims calculate_text_size(const std::string &label, const std::string &font_path, int font_size, int line_thickness)
{
    return ::internal_calculate_text_size(label, font_path, font_size, line_thickness);
}

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

static const char *to_string(rotation_alignment_policy_t policy)
{
    switch (policy)
    {
    case rotation_alignment_policy_t::CENTER:
        return "CENTER";
    case rotation_alignment_policy_t::TOP_LEFT:
        return "TOP_LEFT";
    default:
        return "CENTER";
    }
}

static const char *to_string(font_weight_t weight)
{
    switch (weight)
    {
    case font_weight_t::NORMAL:
        return "NORMAL";
    case font_weight_t::BOLD:
        return "BOLD";
    default:
        return "NORMAL";
    }
}

static nlohmann::json rgba_to_json(const rgba_color_t &c)
{
    return nlohmann::json::array({c.red, c.green, c.blue, c.alpha});
}

void to_json(nlohmann::json &json, const ImageOverlay &overlay)
{
    json = nlohmann::json{{"id", overlay.id},
                          {"x", overlay.x},
                          {"y", overlay.y},
                          {"width", overlay.width},
                          {"height", overlay.height},
                          {"image_path", overlay.image_path},
                          {"z-index", overlay.z_index},
                          {"angle", overlay.angle},
                          {"rotation_policy", to_string(overlay.rotation_alignment_policy)},
                          {"horizontal_alignment", overlay.horizontal_alignment.as_float()},
                          {"vertical_alignment", overlay.vertical_alignment.as_float()}};
}

void to_json(nlohmann::json &json, const TextOverlay &overlay)
{
    json = nlohmann::json{{"id", overlay.id},
                          {"x", overlay.x},
                          {"y", overlay.y},
                          {"label", overlay.label},
                          {"text_color", rgba_to_json(overlay.text_color)},
                          {"background_color", rgba_to_json(overlay.background_color)},
                          {"font_path", overlay.font_path},
                          {"font_size", overlay.font_size},
                          {"line_thickness", overlay.line_thickness},
                          {"outline_size", overlay.outline_size},
                          {"outline_color", rgba_to_json(overlay.outline_color)},
                          {"shadow_color", rgba_to_json(overlay.shadow_color)},
                          {"shadow_offset_x", overlay.shadow_offset_x},
                          {"shadow_offset_y", overlay.shadow_offset_y},
                          {"font_weight", to_string(overlay.font_weight)},
                          {"z-index", overlay.z_index},
                          {"angle", overlay.angle},
                          {"rotation_policy", to_string(overlay.rotation_alignment_policy)},
                          {"horizontal_alignment", overlay.horizontal_alignment.as_float()},
                          {"vertical_alignment", overlay.vertical_alignment.as_float()}};
}

void to_json(nlohmann::json &json, const DateTimeOverlay &overlay)
{
    json = nlohmann::json{{"id", overlay.id},
                          {"x", overlay.x},
                          {"y", overlay.y},
                          {"datetime_format", overlay.datetime_format},
                          {"text_color", rgba_to_json(overlay.text_color)},
                          {"background_color", rgba_to_json(overlay.background_color)},
                          {"font_path", overlay.font_path},
                          {"font_size", overlay.font_size},
                          {"line_thickness", overlay.line_thickness},
                          {"outline_size", overlay.outline_size},
                          {"outline_color", rgba_to_json(overlay.outline_color)},
                          {"shadow_color", rgba_to_json(overlay.shadow_color)},
                          {"shadow_offset_x", overlay.shadow_offset_x},
                          {"shadow_offset_y", overlay.shadow_offset_y},
                          {"font_weight", to_string(overlay.font_weight)},
                          {"z-index", overlay.z_index},
                          {"angle", overlay.angle},
                          {"rotation_policy", to_string(overlay.rotation_alignment_policy)},
                          {"horizontal_alignment", overlay.horizontal_alignment.as_float()},
                          {"vertical_alignment", overlay.vertical_alignment.as_float()}};
}

void to_json(nlohmann::json &json, const CustomOverlay &overlay)
{
    json = nlohmann::json{{"id", overlay.id},
                          {"x", overlay.x},
                          {"y", overlay.y},
                          {"width", overlay.width},
                          {"height", overlay.height},
                          {"z-index", overlay.z_index},
                          {"horizontal_alignment", overlay.horizontal_alignment.as_float()},
                          {"vertical_alignment", overlay.vertical_alignment.as_float()}};
}

} // namespace osd
