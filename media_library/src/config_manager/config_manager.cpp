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

// Includes
#include <nlohmann/json-schema.hpp>
#include <string>

// Media Library Includes
#include "config_manager.hpp"
#include "config_manager_schemas.hpp"
#include "config_type_conversions.hpp"
#include "media_library_logger.hpp"

/* json-parse configurations - with custom error handler */
class config_manager_error_handler : public nlohmann::json_schema::basic_error_handler
{
    void error(const nlohmann::json_pointer<nlohmann::basic_json<>> &pointer, const nlohmann::json &instance,
               const std::string &message) override
    {
        nlohmann::json_schema::basic_error_handler::error(pointer, instance, message);
        LOGGER__ERROR("Configuration Manager encountered an error: {} \nEncountered in: {} \nEncountered instance: {}",
                      message, pointer.to_string(), instance.dump());
    }
};

class ConfigManager::ConfigManagerImpl
{
public:
    /**
     * @brief Constructor for the ConfigManagerImpl module
     *
     */
    ConfigManagerImpl(ConfigSchema schema)
    {

        switch (schema)
        {
        case ConfigSchema::CONFIG_SCHEMA_VISION:
            m_config_validator.set_root_schema(config_schemas::vision_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_ENCODER:
            m_config_validator.set_root_schema(config_schemas::encoder_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE:
            m_config_validator.set_root_schema(config_schemas::multi_resize_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_OSD:
        {
            m_config_validator.set_root_schema(config_schemas::osd_config_schema);
            break;
        }
        case ConfigSchema::CONFIG_SCHEMA_LDC:
            m_config_validator.set_root_schema(config_schemas::ldc_config_schema);
            break;
        default:
            m_config_validator.set_root_schema(config_schemas::vision_config_schema); // Default to the vision_config schema
        }
    };

    /**
     * @brief Destructor for the ConfigManagerImpl module
     */
    ~ConfigManagerImpl(){};

    /**
     * @brief Copy constructor (deleted)
     */
    ConfigManagerImpl(const ConfigManagerImpl &) = delete;

    /**
     * @brief Copy assignment operator (deleted)
     */
    ConfigManagerImpl &operator=(const ConfigManagerImpl &) = delete;

    /**
     * @brief Move constructor
     */
    ConfigManagerImpl(ConfigManagerImpl &&) = delete;

    /**
     * @brief Move assignment
     */
    ConfigManagerImpl &operator=(ConfigManagerImpl &&) = delete;

    /**
     * @brief Validate the user's configuration json string against internal schema
     *
     * @param[in] user_config - the user's configuration (as a json string)
     * @return media_library_return
     */
    media_library_return validate_config_string(const std::string &user_config_string);

    /**
     * @brief Validate a json string and populate a configuration struct
     *
     * @param[in] user_config - the user's configuration (as a json string)
     * @param[out] pre_proc_conf - the user's configuration (as a json string)
     * @return media_library_return
     */
    template <typename TConf>
    media_library_return config_string_to_struct(const std::string &user_config_string,
                                                 TConf &conf);

private:
    nlohmann::json_schema::json_validator m_config_validator;

    /*
     * @brief Validate the user's configuration json against internal schema
     *
     * @param[in] user_config - the user's configuration
     * @return media_library_return
     */
    media_library_return validate_config(const nlohmann::json &user_config);
};

//------------------------ ConfigManager ------------------------

ConfigManager::ConfigManager(ConfigSchema schema)
    : m_config_manager_impl{std::make_unique<ConfigManagerImpl>(schema)}
{
}

ConfigManager::~ConfigManager()
{
    // Defined this in the .cpp file(s) or you will get incomplete type errors
}

media_library_return ConfigManager::validate_configuration(const std::string &user_config_string)
{
    return m_config_manager_impl->validate_config_string(user_config_string);
}

template <typename TConf>
media_library_return ConfigManager::config_string_to_struct(const std::string &user_config_string, TConf &conf)
{
    return m_config_manager_impl->config_string_to_struct<TConf>(user_config_string, conf);
}

// Explicit instantiation for config types (because they were defined in a .cpp file)
template media_library_return ConfigManager::config_string_to_struct<pre_proc_op_configurations>(const std::string &user_config_string, pre_proc_op_configurations &conf);
template media_library_return ConfigManager::config_string_to_struct<multi_resize_config_t>(const std::string &user_config_string, multi_resize_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<ldc_config_t>(const std::string &user_config_string, ldc_config_t &conf);

//------------------------ ConfigManagerImpl ------------------------

media_library_return ConfigManager::ConfigManagerImpl::validate_config(const nlohmann::json &user_config)
{
    config_manager_error_handler err;
    m_config_validator.validate(user_config, err); // validate the document
    if (err)
    {
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManager::ConfigManagerImpl::validate_config_string(const std::string &user_config_string)
{
    const nlohmann::json user_config_json = nlohmann::json::parse(user_config_string, nullptr, false);
    if (user_config_json.is_discarded())
    {
        LOGGER__ERROR("Config Manager failed to parse string as JSON");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return ret = validate_config(user_config_json);
    return ret;
}

template <typename TConf>
media_library_return ConfigManager::ConfigManagerImpl::config_string_to_struct(const std::string &user_config_string,
                                                                               TConf &conf)
{
    // Convert string to JSON
    const nlohmann::json user_config_json = nlohmann::json::parse(user_config_string, nullptr, false);
    if (user_config_json.is_discarded())
    {
        LOGGER__ERROR("Config Manager failed to parse string as JSON");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Validate against internal schema
    media_library_return validation_status = validate_config(user_config_json);
    if (validation_status == MEDIA_LIBRARY_CONFIGURATION_ERROR)
    {
        LOGGER__ERROR("Config Manager failed to validate json against schema");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Convert JSON to struct
    try
    {
        conf = user_config_json.get<TConf>();
    }
    catch (const nlohmann::json::exception &e)
    {
        LOGGER__ERROR("Config Manager failed to convert JSON to struct: {}", e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}