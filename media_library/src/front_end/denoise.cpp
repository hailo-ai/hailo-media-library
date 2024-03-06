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

#include "denoise.hpp"
#include "config_manager.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include <iostream>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <stdint.h>
#include <string>
#include <sys/ioctl.h>
#include <time.h>
#include <tl/expected.hpp>
#include <vector>

class MediaLibraryDenoise::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> create();
    static tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> create(std::string config_string);
    // Constructor
    Impl(media_library_return &status);
    Impl(media_library_return &status, std::string config_string);
    // Destructor
    ~Impl();
    // Move constructor
    Impl(Impl &&) = delete;
    // Move assignment
    Impl &operator=(Impl &&) = delete;

    // Configure the denoise module with new json string
    media_library_return configure(std::string config_string);

    // Configure the denoise module with denoise_config_t and hailort_t object
    media_library_return configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs);

    // get the denoise configurations object
    denoise_config_t &get_denoise_configs();

    // get the hailort configurations object
    hailort_t &get_hailort_configs();

    // get the enabled config status
    bool is_enabled();

    // set the callbacks object
    media_library_return observe(const MediaLibraryDenoise::callbacks_t &callbacks);

private:
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // configuration manager
    std::shared_ptr<ConfigManager> m_denoise_config_manager;
    std::shared_ptr<ConfigManager> m_hailort_config_manager;
    std::vector<MediaLibraryDenoise::callbacks_t> m_callbacks;
    // operation configurations
    denoise_config_t m_denoise_configs;
    hailort_t m_hailort_configs;
    media_library_return reconfigure();
    media_library_return validate_configurations(denoise_config_t &denoise_configs, hailort_t &hailort_configs);
    media_library_return decode_config_json_string(denoise_config_t &denoise_configs,  hailort_t &hailort_configs, std::string config_string);
};

//------------------------ MediaLibraryDenoise ------------------------
tl::expected<std::shared_ptr<MediaLibraryDenoise>, media_library_return> MediaLibraryDenoise::create()
{
    auto impl_expected = Impl::create();
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDenoise>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

tl::expected<std::shared_ptr<MediaLibraryDenoise>, media_library_return> MediaLibraryDenoise::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDenoise>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryDenoise::MediaLibraryDenoise(std::shared_ptr<MediaLibraryDenoise::Impl> impl) : m_impl(impl) {}

MediaLibraryDenoise::~MediaLibraryDenoise() = default;

media_library_return MediaLibraryDenoise::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryDenoise::configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs)
{
    return m_impl->configure(denoise_configs, hailort_configs);
}

denoise_config_t &MediaLibraryDenoise::get_denoise_configs()
{
    return m_impl->get_denoise_configs();
}

hailort_t &MediaLibraryDenoise::get_hailort_configs()
{
    return m_impl->get_hailort_configs();
}

bool MediaLibraryDenoise::is_enabled()
{
    return m_impl->is_enabled();
}

media_library_return MediaLibraryDenoise::observe(const MediaLibraryDenoise::callbacks_t &callbacks)
{
    return m_impl->observe(callbacks);
}

//------------------------ MediaLibraryDenoise::Impl ------------------------
tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> MediaLibraryDenoise::Impl::create()
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDenoise::Impl> denoise = std::make_shared<MediaLibraryDenoise::Impl>(status);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return denoise;
}

tl::expected<std::shared_ptr<MediaLibraryDenoise::Impl>, media_library_return> MediaLibraryDenoise::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDenoise::Impl> denoise = std::make_shared<MediaLibraryDenoise::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return denoise;
}

MediaLibraryDenoise::Impl::Impl(media_library_return &status)
{
    m_configured = false;
    m_denoise_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_DENOISE);
    m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDenoise::Impl::Impl(media_library_return &status, std::string config_string)
{
    m_configured = false;
    m_denoise_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_DENOISE);
    m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    if (decode_config_json_string(m_denoise_configs, m_hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string");
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDenoise::Impl::~Impl()
{
}

media_library_return MediaLibraryDenoise::Impl::decode_config_json_string(denoise_config_t &denoise_configs, hailort_t &hailort_configs, std::string config_string)
{
    media_library_return hailort_status = m_hailort_config_manager->config_string_to_struct<hailort_t>(config_string, hailort_configs);
    if (hailort_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode Hailort config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return denoise_status = m_denoise_config_manager->config_string_to_struct<denoise_config_t>(config_string, denoise_configs);
    if (denoise_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode denoise config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::Impl::configure(std::string config_string)
{
    denoise_config_t denoise_configs;
    hailort_t hailort_configs;
    LOGGER__INFO("Configuring denoise Decoding json string");
    if (decode_config_json_string(denoise_configs, hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(denoise_configs, hailort_configs);
}

media_library_return MediaLibraryDenoise::Impl::validate_configurations(denoise_config_t &denoise_configs, hailort_t &hailort_configs)
{
    // TODO: add validation
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDenoise::Impl::configure(denoise_config_t &denoise_configs, hailort_t &hailort_configs)
{
    LOGGER__INFO("Configuring denoise");
    if (validate_configurations(denoise_configs, hailort_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to validate configurations");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    auto prev_enabled = m_denoise_configs.enabled;
    m_denoise_configs = denoise_configs;
    m_hailort_configs = hailort_configs;
    bool enabled_changed = m_denoise_configs.enabled != prev_enabled;

    // Call observing callbacks in case configuration changed
    for (auto &callbacks : m_callbacks)
    {
        if ((!m_configured || enabled_changed) && callbacks.on_enable_changed)
            callbacks.on_enable_changed(m_denoise_configs.enabled);
    }
    m_configured = true;

    return MEDIA_LIBRARY_SUCCESS;
}

denoise_config_t &MediaLibraryDenoise::Impl::get_denoise_configs()
{
    return m_denoise_configs;
}

hailort_t &MediaLibraryDenoise::Impl::get_hailort_configs()
{
    return m_hailort_configs;
}

bool MediaLibraryDenoise::Impl::is_enabled()
{
    return m_denoise_configs.enabled;
}

media_library_return MediaLibraryDenoise::Impl::observe(const MediaLibraryDenoise::callbacks_t &callbacks)
{
    m_callbacks.push_back(callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}