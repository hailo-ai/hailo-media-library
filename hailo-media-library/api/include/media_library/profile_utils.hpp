#pragma once

#include "media_library_types.hpp"
#include <algorithm>
#include <variant>

/**
 * @brief Get the encoder type from an encoder configuration variant.
 * @param config_variant The encoder configuration variant.
 * @return The encoder type (Hailo, Jpeg, or None).
 */
inline EncoderType get_encoder_type_from_config(const encoder_config_t &config_variant)
{
    return std::visit(
        [](const auto &config) -> EncoderType {
            using T = std::decay_t<decltype(config)>;
            if constexpr (std::is_same_v<T, hailo_encoder_config_t>)
                return EncoderType::Hailo;
            else if constexpr (std::is_same_v<T, jpeg_encoder_config_t>)
                return EncoderType::Jpeg;
            return EncoderType::Hailo; // return hailo as default
        },
        config_variant);
}

/**
 * @brief Check if a stream restart is required when switching between two profiles.
 *
 * Compares resolution, rotation, and encoder type fields between the previous
 * and new profiles. This is a pure data comparison with no server state dependency.
 *
 * @param previous_profile The previously active profile.
 * @param new_profile The new profile to switch to.
 * @return true if a stream restart is required, false otherwise.
 */
inline bool stream_restart_required(const config_profile_t &previous_profile, const config_profile_t &new_profile)
{
    bool restart_required = false;

    // Resolution changes
    for (const auto &resolution : previous_profile.application_settings.application_input_streams.resolutions)
    {
        if (std::find_if(new_profile.application_settings.application_input_streams.resolutions.begin(),
                         new_profile.application_settings.application_input_streams.resolutions.end(),
                         [&resolution](const auto &res) {
                             return resolution.dimensions_and_aspect_ratio_equal(res);
                         }) == new_profile.application_settings.application_input_streams.resolutions.end())
        {
            restart_required = true;
            break;
        }
    }

    // Rotation changes
    restart_required |= previous_profile.application_settings.rotation.effective_value() !=
                        new_profile.application_settings.rotation.effective_value();

    // Encoder type changes (H.26x <-> JPEG)
    auto prev_encoder_map = previous_profile.to_encoder_config_map();
    auto new_encoder_map = new_profile.to_encoder_config_map();

    for (const auto &entry : new_encoder_map)
    {
        const auto &stream_id = entry.first;
        if (prev_encoder_map.find(stream_id) != prev_encoder_map.end())
        {
            EncoderType prev_type = get_encoder_type_from_config(prev_encoder_map[stream_id]);
            EncoderType new_type = get_encoder_type_from_config(new_encoder_map[stream_id]);
            if (prev_type != new_type)
            {
                restart_required = true;
                break;
            }
        }
    }

    return restart_required;
}
