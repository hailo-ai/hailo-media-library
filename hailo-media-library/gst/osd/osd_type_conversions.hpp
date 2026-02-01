#pragma once

#include "osd_types.hpp"

#include "media_library/media_library_types.hpp"

#include <optional>
#include <string>

namespace osd::type_conversions
{

inline osd::rgba_color_t to_osd(const ::rgba_color_t &c)
{
    return osd::rgba_color_t{c.red, c.green, c.blue, c.alpha};
}

inline ::rgba_color_t to_media(const osd::rgba_color_t &c)
{
    return ::rgba_color_t{c.red, c.green, c.blue, c.alpha};
}

inline osd::rotation_alignment_policy_t to_osd(const ::rotation_alignment_policy_t policy)
{
    return static_cast<osd::rotation_alignment_policy_t>(static_cast<int>(policy));
}

inline ::rotation_alignment_policy_t to_media(const osd::rotation_alignment_policy_t policy)
{
    return static_cast<::rotation_alignment_policy_t>(static_cast<int>(policy));
}

inline osd::font_weight_t to_osd(const ::font_weight_t weight)
{
    return static_cast<osd::font_weight_t>(static_cast<int>(weight));
}

inline std::optional<osd::font_weight_t> to_osd(const std::optional<::font_weight_t> &weight)
{
    if (!weight)
    {
        return std::nullopt;
    }
    return to_osd(weight.value());
}

inline ::font_weight_t to_media(const osd::font_weight_t weight)
{
    return static_cast<::font_weight_t>(static_cast<int>(weight));
}

inline osd::HorizontalAlignment to_osd(const ::HorizontalAlignment &a)
{
    auto created = osd::HorizontalAlignment::create(a.as_float());
    if (!created.has_value())
    {
        return osd::HorizontalAlignment::LEFT;
    }
    return created.value();
}

inline osd::HorizontalAlignment to_osd(const std::optional<::HorizontalAlignment> &a,
                                       osd::HorizontalAlignment fallback = osd::HorizontalAlignment::LEFT)
{
    if (!a)
    {
        return fallback;
    }
    return to_osd(a.value());
}

inline osd::VerticalAlignment to_osd(const ::VerticalAlignment &a)
{
    auto created = osd::VerticalAlignment::create(a.as_float());
    if (!created.has_value())
    {
        return osd::VerticalAlignment::TOP;
    }
    return created.value();
}

inline osd::VerticalAlignment to_osd(const std::optional<::VerticalAlignment> &a,
                                     osd::VerticalAlignment fallback = osd::VerticalAlignment::TOP)
{
    if (!a)
    {
        return fallback;
    }
    return to_osd(a.value());
}

inline std::optional<::HorizontalAlignment> to_media_optional(const osd::HorizontalAlignment &a)
{
    const float v = a.as_float();
    if (v == osd::HorizontalAlignment::LEFT.as_float())
    {
        return ::HorizontalAlignment::LEFT;
    }
    if (v == osd::HorizontalAlignment::CENTER.as_float())
    {
        return ::HorizontalAlignment::CENTER;
    }
    if (v == osd::HorizontalAlignment::RIGHT.as_float())
    {
        return ::HorizontalAlignment::RIGHT;
    }

    // Media-library alignment currently supports only {0.0, 0.5, 1.0}.
    // Pick the nearest discrete value.
    if (v < 0.25f)
    {
        return ::HorizontalAlignment::LEFT;
    }
    if (v < 0.75f)
    {
        return ::HorizontalAlignment::CENTER;
    }
    return ::HorizontalAlignment::RIGHT;
}

inline std::optional<::VerticalAlignment> to_media_optional(const osd::VerticalAlignment &a)
{
    const float v = a.as_float();
    if (v == osd::VerticalAlignment::TOP.as_float())
    {
        return ::VerticalAlignment::TOP;
    }
    if (v == osd::VerticalAlignment::CENTER.as_float())
    {
        return ::VerticalAlignment::CENTER;
    }
    if (v == osd::VerticalAlignment::BOTTOM.as_float())
    {
        return ::VerticalAlignment::BOTTOM;
    }

    if (v < 0.25f)
    {
        return ::VerticalAlignment::TOP;
    }
    if (v < 0.75f)
    {
        return ::VerticalAlignment::CENTER;
    }
    return ::VerticalAlignment::BOTTOM;
}

inline bool is_disabled_color(const osd::rgba_color_t &c)
{
    return c.red < 0 || c.green < 0 || c.blue < 0 || c.alpha < 0;
}

inline osd::ImageOverlay to_osd(const ::ImageOverlay &in)
{
    osd::ImageOverlay out;
    out.id = in.id;
    out.x = in.x;
    out.y = in.y;
    out.z_index = in.z_index;
    out.angle = in.angle;
    out.rotation_alignment_policy = to_osd(in.rotation_alignment_policy);
    out.horizontal_alignment = to_osd(in.horizontal_alignment, osd::HorizontalAlignment::LEFT);
    out.vertical_alignment = to_osd(in.vertical_alignment, osd::VerticalAlignment::TOP);

    out.width = in.width;
    out.height = in.height;
    out.image_path = in.image_path;
    return out;
}

inline osd::TextOverlay to_osd(const ::TextOverlay &in)
{
    osd::TextOverlay out; // pulls BaseTextOverlay defaults

    out.id = in.id;
    out.x = in.x;
    out.y = in.y;
    out.z_index = in.z_index;
    out.angle = in.angle;
    out.rotation_alignment_policy = to_osd(in.rotation_alignment_policy);
    out.horizontal_alignment = to_osd(in.horizontal_alignment, osd::HorizontalAlignment::LEFT);
    out.vertical_alignment = to_osd(in.vertical_alignment, osd::VerticalAlignment::TOP);

    out.label = in.label;
    out.text_color = to_osd(in.text_color);

    if (in.background_color)
    {
        out.background_color = to_osd(in.background_color.value());
    }

    if (!in.font_path.empty())
    {
        out.font_path = in.font_path;
    }

    out.font_size = static_cast<float>(in.font_size);

    if (in.line_thickness)
    {
        out.line_thickness = in.line_thickness.value();
    }

    if (in.shadow_color)
    {
        out.shadow_color = to_osd(in.shadow_color.value());
    }

    if (in.shadow_offset_x)
    {
        out.shadow_offset_x = in.shadow_offset_x.value();
    }

    if (in.shadow_offset_y)
    {
        out.shadow_offset_y = in.shadow_offset_y.value();
    }

    if (in.font_weight)
    {
        out.font_weight = to_osd(in.font_weight.value());
    }

    if (in.outline_size)
    {
        out.outline_size = in.outline_size.value();
    }

    if (in.outline_color)
    {
        out.outline_color = to_osd(in.outline_color.value());
    }

    return out;
}

inline osd::DateTimeOverlay to_osd(const ::DateTimeOverlay &in)
{
    osd::DateTimeOverlay out; // pulls BaseTextOverlay + DEFAULT_DATETIME_STRING

    out.id = in.id;
    out.x = in.x;
    out.y = in.y;
    out.z_index = in.z_index;
    out.angle = in.angle;
    out.rotation_alignment_policy = to_osd(in.rotation_alignment_policy);
    out.horizontal_alignment = to_osd(in.horizontal_alignment, osd::HorizontalAlignment::LEFT);
    out.vertical_alignment = to_osd(in.vertical_alignment, osd::VerticalAlignment::TOP);
    out.text_color = to_osd(in.text_color);

    if (in.background_color)
    {
        out.background_color = to_osd(in.background_color.value());
    }

    if (!in.font_path.empty())
    {
        out.font_path = in.font_path;
    }

    out.font_size = static_cast<float>(in.font_size);

    if (in.line_thickness)
    {
        out.line_thickness = in.line_thickness.value();
    }

    if (in.shadow_color)
    {
        out.shadow_color = to_osd(in.shadow_color.value());
    }

    if (in.shadow_offset_x)
    {
        out.shadow_offset_x = in.shadow_offset_x.value();
    }

    if (in.shadow_offset_y)
    {
        out.shadow_offset_y = in.shadow_offset_y.value();
    }

    if (in.font_weight)
    {
        out.font_weight = to_osd(in.font_weight.value());
    }

    if (in.outline_size)
    {
        out.outline_size = in.outline_size.value();
    }

    if (in.outline_color)
    {
        out.outline_color = to_osd(in.outline_color.value());
    }

    if (in.datetime_format && !in.datetime_format->empty())
    {
        out.datetime_format = in.datetime_format.value();
    }
    else
    {
        out.datetime_format = DEFAULT_DATETIME_STRING;
    }

    return out;
}

inline std::optional<::rgba_color_t> to_media_optional_color(const osd::rgba_color_t &c)
{
    if (is_disabled_color(c))
    {
        return std::nullopt;
    }
    return to_media(c);
}

inline ::ImageOverlay to_media(const osd::ImageOverlay &in)
{
    ::ImageOverlay out;
    out.id = in.id;
    out.x = in.x;
    out.y = in.y;
    out.z_index = in.z_index;
    out.angle = in.angle;
    out.rotation_alignment_policy = to_media(in.rotation_alignment_policy);
    out.horizontal_alignment = to_media_optional(in.horizontal_alignment);
    out.vertical_alignment = to_media_optional(in.vertical_alignment);

    out.width = in.width;
    out.height = in.height;
    out.image_path = in.image_path;
    return out;
}

inline ::TextOverlay to_media(const osd::TextOverlay &in)
{
    ::TextOverlay out;
    out.id = in.id;
    out.x = in.x;
    out.y = in.y;
    out.z_index = in.z_index;
    out.angle = in.angle;
    out.rotation_alignment_policy = to_media(in.rotation_alignment_policy);

    out.horizontal_alignment = to_media_optional(in.horizontal_alignment);
    out.vertical_alignment = to_media_optional(in.vertical_alignment);

    out.label = in.label;
    out.text_color = to_media(in.text_color);
    out.background_color = to_media_optional_color(in.background_color);
    out.font_path = in.font_path;
    out.font_size = static_cast<int>(in.font_size);
    out.line_thickness = in.line_thickness;

    out.shadow_color = to_media_optional_color(in.shadow_color);
    out.shadow_offset_x = in.shadow_offset_x;
    out.shadow_offset_y = in.shadow_offset_y;

    out.font_weight = to_media(in.font_weight);
    out.outline_size = in.outline_size;
    out.outline_color = to_media_optional_color(in.outline_color);
    return out;
}

inline ::DateTimeOverlay to_media(const osd::DateTimeOverlay &in)
{
    ::DateTimeOverlay out;
    out.id = in.id;
    out.x = in.x;
    out.y = in.y;
    out.z_index = in.z_index;
    out.angle = in.angle;
    out.rotation_alignment_policy = to_media(in.rotation_alignment_policy);

    out.horizontal_alignment = to_media_optional(in.horizontal_alignment);
    out.vertical_alignment = to_media_optional(in.vertical_alignment);

    out.text_color = to_media(in.text_color);
    out.background_color = to_media_optional_color(in.background_color);
    out.font_path = in.font_path;
    out.font_size = static_cast<int>(in.font_size);
    out.line_thickness = in.line_thickness;

    out.shadow_color = to_media_optional_color(in.shadow_color);
    out.shadow_offset_x = in.shadow_offset_x;
    out.shadow_offset_y = in.shadow_offset_y;

    out.font_weight = to_media(in.font_weight);
    out.outline_size = in.outline_size;
    out.outline_color = to_media_optional_color(in.outline_color);

    out.datetime_format = in.datetime_format;
    return out;
}

} // namespace osd::type_conversions
