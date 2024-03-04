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

#include "defog.hpp"
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

class MediaLibraryDefog::Impl final
{
public:
    static tl::expected<std::shared_ptr<MediaLibraryDefog::Impl>, media_library_return>
    create(std::string config_string);
    // Constructor
    Impl(media_library_return &status, std::string config_string);
    // Destructor
    ~Impl();
    // Move constructor
    Impl(Impl &&) = delete;
    // Move assignment
    Impl &operator=(Impl &&) = delete;

    // Configure the defog module with new json string
    media_library_return configure(std::string config_string);

    // Configure the defog module with defog_config_t and hailort_t object
    media_library_return configure(defog_config_t &defog_configs, hailort_t &hailort_configs);

    // get the defog configurations object
    defog_config_t &get_defog_configs();

    // get the hailort configurations object
    hailort_t &get_hailort_configs();

    // get the enabled config status
    bool is_enabled();

private:
    // configured flag - to determine if first configuration was done
    bool m_configured;
    // configuration manager
    std::shared_ptr<ConfigManager> m_defog_config_manager;
    std::shared_ptr<ConfigManager> m_hailort_config_manager;
    // operation configurations
    defog_config_t m_defog_configs;
    hailort_t m_hailort_configs;
    media_library_return reconfigure();
    media_library_return validate_configurations(defog_config_t &defog_configs, hailort_t &hailort_configs);
    media_library_return decode_config_json_string(defog_config_t &defog_configs,  hailort_t &hailort_configs, std::string config_string);
};

//------------------------ MediaLibraryDefog ------------------------
tl::expected<std::shared_ptr<MediaLibraryDefog>, media_library_return> MediaLibraryDefog::create(std::string config_string)
{
    auto impl_expected = Impl::create(config_string);
    if (impl_expected.has_value())
        return std::make_shared<MediaLibraryDefog>(impl_expected.value());
    else
        return tl::make_unexpected(impl_expected.error());
}

MediaLibraryDefog::MediaLibraryDefog(std::shared_ptr<MediaLibraryDefog::Impl> impl) : m_impl(impl) {}

MediaLibraryDefog::~MediaLibraryDefog() = default;

media_library_return MediaLibraryDefog::configure(std::string config_string)
{
    return m_impl->configure(config_string);
}

media_library_return MediaLibraryDefog::configure(defog_config_t &defog_configs, hailort_t &hailort_configs)
{
    return m_impl->configure(defog_configs, hailort_configs);
}

defog_config_t &MediaLibraryDefog::get_defog_configs()
{
    return m_impl->get_defog_configs();
}

hailort_t &MediaLibraryDefog::get_hailort_configs()
{
    return m_impl->get_hailort_configs();
}

bool MediaLibraryDefog::is_enabled()
{
    return m_impl->is_enabled();
}

//------------------------ MediaLibraryDefog::Impl ------------------------

tl::expected<std::shared_ptr<MediaLibraryDefog::Impl>, media_library_return> MediaLibraryDefog::Impl::create(std::string config_string)
{
    media_library_return status = MEDIA_LIBRARY_UNINITIALIZED;
    std::shared_ptr<MediaLibraryDefog::Impl> defog = std::make_shared<MediaLibraryDefog::Impl>(status, config_string);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return defog;
}

MediaLibraryDefog::Impl::Impl(media_library_return &status, std::string config_string)
{
    m_configured = false;
    m_defog_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_DEFOG);
    m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    if (decode_config_json_string(m_defog_configs, m_hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string");
        status = MEDIA_LIBRARY_INVALID_ARGUMENT;
        return;
    }

    status = MEDIA_LIBRARY_SUCCESS;
}

MediaLibraryDefog::Impl::~Impl()
{
}

media_library_return MediaLibraryDefog::Impl::decode_config_json_string(defog_config_t &defog_configs, hailort_t &hailort_configs, std::string config_string)
{
    media_library_return hailort_status = m_hailort_config_manager->config_string_to_struct<hailort_t>(config_string, hailort_configs);
    if (hailort_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode Hailort config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return defog_status = m_defog_config_manager->config_string_to_struct<defog_config_t>(config_string, defog_configs);
    if (defog_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode defog config from json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDefog::Impl::configure(std::string config_string)
{
    defog_config_t defog_configs;
    hailort_t hailort_configs;
    LOGGER__INFO("Configuring defog Decoding json string");
    if (decode_config_json_string(defog_configs, hailort_configs, config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to decode json string: {}", config_string);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return configure(defog_configs, hailort_configs);
}

media_library_return MediaLibraryDefog::Impl::validate_configurations(defog_config_t &defog_configs, hailort_t &hailort_configs)
{
    // TODO: add validation
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibraryDefog::Impl::configure(defog_config_t &defog_configs, hailort_t &hailort_configs)
{
    LOGGER__INFO("Configuring defog");
    if (validate_configurations(defog_configs, hailort_configs) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to validate configurations");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    m_defog_configs = defog_configs;
    m_hailort_configs = hailort_configs;
    m_configured = true;
    return MEDIA_LIBRARY_SUCCESS;
}

defog_config_t &MediaLibraryDefog::Impl::get_defog_configs()
{
    return m_defog_configs;
}

hailort_t &MediaLibraryDefog::Impl::get_hailort_configs()
{
    return m_hailort_configs;
}

bool MediaLibraryDefog::Impl::is_enabled()
{
    return m_defog_configs.enabled;
}
