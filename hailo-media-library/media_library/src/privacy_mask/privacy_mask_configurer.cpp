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

#include <sys/types.h>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "config_parser.hpp"
#include "media_library_types.hpp"
#include "privacy_mask.hpp"
#include "config_manager.hpp"
#include "media_library_logger.hpp"
#include "privacy_mask_types.hpp"
#include "encoder_config_types.hpp"

#define MODULE_NAME LoggerType::PrivacyMask

using namespace privacy_mask_types;

PrivacyMaskConfigurer::PrivacyMaskConfigurer() = default;
PrivacyMaskConfigurer::~PrivacyMaskConfigurer() = default;

void PrivacyMaskConfigurer::set_stream_id(const std::string &stream_id)
{
    m_stream_id = stream_id;
}

void PrivacyMaskConfigurer::set_config_manager_interactor(ConfigManagerInteractor *config_manager_interactor)
{
    m_config_manager_interactor = config_manager_interactor;
}

media_library_return PrivacyMaskConfigurer::add_static_privacy_mask(const polygon &privacy_mask)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    if (privacy_mask.vertices.size() > MAX_NUM_OF_VERTICES_IN_POLYGON)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Polygon cannot have more than {} vertices", MAX_NUM_OF_VERTICES_IN_POLYGON);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        const bool is_enabled = true;
        stream_it->second.masking.static_privacy_mask_config = static_privacy_mask_config_t{is_enabled, {}};
    }

    if (static_privacy_mask_config->masks.size() >= MAX_NUM_OF_STATIC_PRIVACY_MASKS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Max number of privacy masks reached {}", MAX_NUM_OF_STATIC_PRIVACY_MASKS);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    static_privacy_mask_config->masks.emplace_back(privacy_mask);

    m_config_manager_interactor->set_profile(current_profile);

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskConfigurer::set_static_privacy_mask(const polygon &privacy_mask)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    if (privacy_mask.vertices.size() > MAX_NUM_OF_VERTICES_IN_POLYGON)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Polygon cannot have more than {} vertices", MAX_NUM_OF_VERTICES_IN_POLYGON);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }

    // find the specific privacy mask
    auto it = std::find_if(
        static_privacy_mask_config->masks.begin(), static_privacy_mask_config->masks.end(),
        [&privacy_mask](const polygon &existing_privacy_mask) { return existing_privacy_mask.id == privacy_mask.id; });

    if (it == static_privacy_mask_config->masks.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask with id {} not found", privacy_mask.id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    static_privacy_mask_config->masks.erase(it);
    static_privacy_mask_config->masks.emplace_back(privacy_mask);
    m_config_manager_interactor->set_profile(current_profile);

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskConfigurer::remove_static_privacy_mask(const std::string &id)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return MEDIA_LIBRARY_SUCCESS;
    }
    auto it = std::find_if(static_privacy_mask_config->masks.begin(), static_privacy_mask_config->masks.end(),
                           [&id](const polygon &privacy_mask) { return privacy_mask.id == id; });

    if (it == static_privacy_mask_config->masks.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask with id {} not found", id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    static_privacy_mask_config->masks.erase(it);
    m_config_manager_interactor->set_profile(current_profile);

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskConfigurer::set_color(const rgb_color_t &color)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    stream_it->second.masking.mask_type = PrivacyMaskType::COLOR;
    stream_it->second.masking.color_value = color;
    m_config_manager_interactor->set_profile(current_profile);

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskConfigurer::set_pixelization_size(PixelizationSize size)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    if (size < 2 || size > 64)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Pixelization size must be a number between 2 and 64");
        return media_library_return::MEDIA_LIBRARY_INVALID_ARGUMENT;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    stream_it->second.masking.mask_type = PrivacyMaskType::PIXELIZATION;
    stream_it->second.masking.pixelization_size = size;
    m_config_manager_interactor->set_profile(current_profile);

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<rgb_color_t, media_library_return> PrivacyMaskConfigurer::get_color()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto privacy_mask_type = stream_it->second.masking.mask_type;
    if (privacy_mask_type != PrivacyMaskType::COLOR)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask type is not set to COLOR");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return stream_it->second.masking.color_value;
}

tl::expected<PixelizationSize, media_library_return> PrivacyMaskConfigurer::get_pixelization_size()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto privacy_mask_type = stream_it->second.masking.mask_type;
    if (privacy_mask_type != PrivacyMaskType::PIXELIZATION)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask type is not set to PIXELIZATION");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return stream_it->second.masking.pixelization_size;
}

tl::expected<polygon, media_library_return> PrivacyMaskConfigurer::get_static_privacy_mask(const std::string &id)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto it = std::find_if(static_privacy_mask_config->masks.begin(), static_privacy_mask_config->masks.end(),
                           [&id](const polygon &privacy_mask) { return privacy_mask.id == id; });
    if (it == static_privacy_mask_config->masks.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Privacy mask with id {} not found", id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return *it;
}

tl::expected<std::pair<uint, uint>, media_library_return> PrivacyMaskConfigurer::get_frame_size()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    encoder_config_t &stream_config = stream_it->second.encoding;
    if (std::holds_alternative<hailo_encoder_config_t>(stream_config))
    {
        return std::make_pair(std::get<hailo_encoder_config_t>(stream_config).input_stream.width,
                              std::get<hailo_encoder_config_t>(stream_config).input_stream.height);
    }
    if (std::holds_alternative<jpeg_encoder_config_t>(stream_config))
    {
        return std::make_pair(std::get<jpeg_encoder_config_t>(stream_config).input_stream.width,
                              std::get<jpeg_encoder_config_t>(stream_config).input_stream.height);
    }

    LOGGER__MODULE__ERROR(MODULE_NAME, "Unsupported encoder config type for stream id {}", m_stream_id);
    return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
}

media_library_return PrivacyMaskConfigurer::set_frame_size(const uint, const uint)
{
    LOGGER__MODULE__WARN(MODULE_NAME, "set_frame_size does not have an effect in the current implementation, rotation "
                                      "is handled without calling this function");
    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

media_library_return PrivacyMaskConfigurer::clear_all_static_privacy_masks()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }
    static_privacy_mask_config->masks.clear();
    m_config_manager_interactor->set_profile(current_profile);

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

tl::expected<std::vector<polygon>, media_library_return> PrivacyMaskConfigurer::get_all_static_privacy_masks()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return tl::make_unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    return static_privacy_mask_config->masks;
}

media_library_return PrivacyMaskConfigurer::set_static_mask_enabled(bool enable)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    auto &stream_privacy_mask_config = stream_it->second.masking;
    if (!stream_privacy_mask_config.static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    auto &static_config = stream_privacy_mask_config.static_privacy_mask_config.value();
    if (static_config.enabled == enable)
    {
        return MEDIA_LIBRARY_SUCCESS;
    }
    static_config.enabled = enable;
    m_config_manager_interactor->set_profile(current_profile);
    return MEDIA_LIBRARY_SUCCESS;
}

bool PrivacyMaskConfigurer::is_static_mask_enabled()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return false;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return false;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return false;
    }
    auto &static_privacy_mask_config = stream_it->second.masking.static_privacy_mask_config;
    if (!static_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "Static privacy mask config not found for stream id {}", m_stream_id);
        return false;
    }
    return static_privacy_mask_config->enabled;
}

media_library_return PrivacyMaskConfigurer::set_dynamic_mask_enabled(bool enable)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    auto &stream_privacy_mask_config = stream_it->second.masking;
    if (!stream_privacy_mask_config.dynamic_privacy_mask_config.has_value())
    {
        const bool is_enabled = true;
        stream_it->second.masking.dynamic_privacy_mask_config =
            dynamic_privacy_mask_config_t{is_enabled, {}, 0, 10000, 34, AnalyticsQueryType::WithinDelta};
    }
    auto &dynamic_config = stream_privacy_mask_config.dynamic_privacy_mask_config.value();
    dynamic_config.enabled = enable;
    m_config_manager_interactor->set_profile(current_profile);

    return MEDIA_LIBRARY_SUCCESS;
}

bool PrivacyMaskConfigurer::is_dynamic_mask_enabled()
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return false;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return false;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return false;
    }
    auto &dynamic_privacy_mask_config = stream_it->second.masking.dynamic_privacy_mask_config;
    if (!dynamic_privacy_mask_config.has_value())
    {
        LOGGER__MODULE__WARN(MODULE_NAME, "dynamic privacy mask config not found for stream id {}", m_stream_id);
        return false;
    }
    return dynamic_privacy_mask_config->enabled;
}

media_library_return PrivacyMaskConfigurer::configure(const std::string &config)
{
    // check if the config has ' at the beginning and end of the string. if so, remove them
    std::string clean_config = config; // config is const, so we need to copy it
    if (clean_config[0] == '\'' && clean_config[clean_config.size() - 1] == '\'')
    {
        clean_config = clean_config.substr(1, config.size() - 2);
    }

    ConfigParser config_parser(CONFIG_SCHEMA_PRIVACY_MASK);
    if (config_parser.validate_configuration(clean_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate configuration");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto config_json = nlohmann::json::parse(clean_config)["masking"];
    std::string privacy_mask_config_string = config_json.dump();

    auto privacy_mask_config = std::make_unique<privacy_mask_config_t>();
    if (config_parser.config_string_to_struct<privacy_mask_config_t>(privacy_mask_config_string,
                                                                     *privacy_mask_config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to convert config string to struct");
        return MEDIA_LIBRARY_INVALID_ARGUMENT;
    }

    return configure(privacy_mask_config);
}

media_library_return PrivacyMaskConfigurer::configure(const std::unique_ptr<privacy_mask_config_t> &config)
{
    if (m_config_manager_interactor == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config manager interactor is not set");
        return MEDIA_LIBRARY_ERROR;
    }
    auto current_profile_opt = m_config_manager_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current profile");
        return MEDIA_LIBRARY_ERROR;
    }
    config_profile_t current_profile = *current_profile_opt.value();
    auto stream_it = current_profile.encoded_output_streams.find(m_stream_id);
    if (stream_it == current_profile.encoded_output_streams.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Stream id {} not found in current profile", m_stream_id);
        return MEDIA_LIBRARY_ERROR;
    }
    stream_it->second.masking = *config;
    m_config_manager_interactor->set_profile(current_profile);

    return MEDIA_LIBRARY_SUCCESS;
}
