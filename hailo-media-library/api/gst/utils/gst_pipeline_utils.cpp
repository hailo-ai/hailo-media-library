#include "gst_pipeline_utils.hpp"
#include "gstmedialibcommon.hpp"
#include <cctype>
#include <string>

namespace hailo::gst_api
{

tl::expected<std::string, media_library_return> get_parent_pipeline_name(GstElement *element)
{
    if (element == nullptr)
    {
        return tl::unexpected(MEDIA_LIBRARY_INVALID_ARGUMENT);
    }

    GstObjectPtr cur(GST_OBJECT_CAST(gst_object_ref(element)));
    while (cur)
    {
        if (GST_IS_PIPELINE(cur.get()))
        {
            std::string pipeline_name = glib_cpp::get_name(cur);
            if (pipeline_name.empty())
            {
                return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
            }
            return pipeline_name;
        }

        GstObjectPtr parent(gst_object_get_parent(cur));
        if (!parent)
        {
            return tl::unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
        }
        cur = std::move(parent);
    }

    return tl::unexpected(MEDIA_LIBRARY_ERROR);
}

bool looks_like_json(const std::string &json_candidate)
{
    size_t i = 0;
    while (i < json_candidate.size() && std::isspace((unsigned char)json_candidate[i]))
        i++;
    return (i < json_candidate.size() && (json_candidate[i] == '{' || json_candidate[i] == '['));
}

std::string stream_id_from_pad_name(const gchar *pad_name)
{
    return std::string(pad_name ? pad_name : "");
}

// Extract input_config from encoder config variant (hailo_encoder or jpeg_encoder)
std::optional<input_config_t> get_input_config_from_encoder(const encoder_config_t &enc_config)
{
    if (std::holds_alternative<hailo_encoder_config_t>(enc_config))
        return std::get<hailo_encoder_config_t>(enc_config).input_stream;
    if (std::holds_alternative<jpeg_encoder_config_t>(enc_config))
        return std::get<jpeg_encoder_config_t>(enc_config).input_stream;
    return std::nullopt;
}

// Format available stream IDs for error messages
std::string format_available_streams_ids(const config_profile_t &profile)
{
    std::string result = "[";
    bool first = true;
    for (const auto &[id, _] : profile.encoded_output_streams)
    {
        if (!first)
            result += ", ";
        result += id;
        first = false;
    }
    result += "]";
    return result;
}

} // namespace hailo::gst_api
