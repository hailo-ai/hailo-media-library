#include "media_library_types.hpp"

#define MODULE_NAME LoggerType::Api

const HorizontalAlignment HorizontalAlignment::LEFT = HorizontalAlignment(0.0);
const HorizontalAlignment HorizontalAlignment::CENTER = HorizontalAlignment(0.5);
const HorizontalAlignment HorizontalAlignment::RIGHT = HorizontalAlignment(1.0);

const VerticalAlignment VerticalAlignment::TOP = VerticalAlignment(0.0);
const VerticalAlignment VerticalAlignment::CENTER = VerticalAlignment(0.5);
const VerticalAlignment VerticalAlignment::BOTTOM = VerticalAlignment(1.0);

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

BaseTextOverlay::BaseTextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                                 rgba_color_t _background_color, int _font_size, int _line_thickness,
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

TextOverlay::TextOverlay(std::string _id, float _x, float _y, std::string _label, rgba_color_t _text_color,
                         rgba_color_t _background_color, int _font_size, int _line_thickness, unsigned int _z_index,
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
DateTimeOverlay::DateTimeOverlay(std::string _id, float _x, float _y, std::string _datetime_format,
                                 rgba_color_t _text_color, rgba_color_t _background_color, std::string _font_path,
                                 int _font_size, int _line_thickness, unsigned int _z_index, unsigned int _angle,
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
