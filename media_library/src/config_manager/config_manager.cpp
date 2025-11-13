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

// Includes
#include <nlohmann/json-schema.hpp>
#include <string>

// Media Library Includes
#include "config_manager.hpp"
#include "config_manager_schemas.hpp"
#include "config_type_conversions.hpp"
#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "media_library_types.hpp"

#define MODULE_NAME LoggerType::Config

/* json-parse configurations - with custom error handler */
class config_manager_error_handler : public nlohmann::json_schema::basic_error_handler
{
    void error(const nlohmann::json_pointer<nlohmann::basic_json<>> &pointer, const nlohmann::json &instance,
               const std::string &message) override
    {
        nlohmann::json_schema::basic_error_handler::error(pointer, instance, message);
        LOGGER__MODULE__ERROR(
            MODULE_NAME,
            "Configuration Manager encountered an error: {} \nEncountered in: {} \nEncountered instance: {}", message,
            pointer.to_string(), instance.dump());
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
        case ConfigSchema::CONFIG_SCHEMA_ENCODER:
            m_config_validator.set_root_schema(config_schemas::encoder_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE:
            m_config_validator.set_root_schema(config_schemas::multi_resize_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_OSD:
            m_config_validator.set_root_schema(config_schemas::osd_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_LDC:
            m_config_validator.set_root_schema(config_schemas::ldc_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_VSM:
            m_config_validator.set_root_schema(config_schemas::vsm_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_HAILORT:
            m_config_validator.set_root_schema(config_schemas::hailort_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_ISP:
            m_config_validator.set_root_schema(config_schemas::isp_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_ISP_CONFIG:
            m_config_validator.set_root_schema(config_schemas::isp_config_files_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_HDR:
            m_config_validator.set_root_schema(config_schemas::hdr_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_DENOISE:
            m_config_validator.set_root_schema(config_schemas::denoise_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_INPUT_VIDEO:
            m_config_validator.set_root_schema(config_schemas::input_video_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_FRONTEND:
            m_config_validator.set_root_schema(config_schemas::frontend_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_MEDIALIB_CONFIG:
            m_config_validator.set_root_schema(config_schemas::medialib_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_PROFILE:
            m_config_validator.set_root_schema(config_schemas::profile_config_schema);
            break;
        }
    };

    /**
     * @brief Destructor for the ConfigManagerImpl module
     */
    ~ConfigManagerImpl() {};

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
    media_library_return config_string_to_struct(const std::string &user_config_string, TConf &conf);

    /**
     * @brief Convert a configuration struct to a json string
     *
     * @param[in] conf - the configuration struct
     * @return std::string
     */
    template <typename TConf> std::string config_struct_to_string(const TConf &conf);

    /**
     * @brief Retrieve an entry from an input JSON string
     *
     * @param[in] config_string - the user's configuration (as a json string)
     * @param[out] entry - the entry name to retrieve
     * @return tl::expected<std::string, media_library_return>
     */
    static tl::expected<std::string, media_library_return> parse_config(std::string config_string, std::string entry);

    /**
     * @brief Retrieve the encoder type from an input JSON configuration
     *
     * @param[in] config_json - the user's configuration (as a json object)
     * @return EncoderType
     */
    static EncoderType get_encoder_type(const nlohmann::json &config_json);

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

ConfigManager::ConfigManager(ConfigSchema schema) : m_config_manager_impl{std::make_unique<ConfigManagerImpl>(schema)}
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

template <typename TConf> std::string ConfigManager::config_struct_to_string(const TConf &conf)
{
    return m_config_manager_impl->config_struct_to_string(conf);
}

// Explicit instantiation for config types (because they were defined in a .cpp file)
template media_library_return ConfigManager::config_string_to_struct<input_video_config_t>(
    const std::string &user_config_string, input_video_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<multi_resize_config_t>(
    const std::string &user_config_string, multi_resize_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<eis_config_t>(
    const std::string &user_config_string, eis_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<gyro_config_t>(
    const std::string &user_config_string, gyro_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<ldc_config_t>(
    const std::string &user_config_string, ldc_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<denoise_config_t>(
    const std::string &user_config_string, denoise_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<isp_t>(const std::string &user_config_string,
                                                                            isp_t &conf);
template media_library_return ConfigManager::config_string_to_struct<hailort_t>(const std::string &user_config_string,
                                                                                hailort_t &conf);
template media_library_return ConfigManager::config_string_to_struct<hdr_config_t>(
    const std::string &user_config_string, hdr_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<encoder_config_t>(
    const std::string &user_config_string, encoder_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<vsm_config_t>(
    const std::string &user_config_string, vsm_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<codec_config_t>(
    const std::string &user_config_string, codec_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<frontend_config_t>(
    const std::string &user_config_string, frontend_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<medialib_config_t>(
    const std::string &user_config_string, medialib_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<profile_config_t>(
    const std::string &user_config_string, profile_config_t &conf);

template std::string ConfigManager::config_struct_to_string<input_video_config_t>(const input_video_config_t &conf);
template std::string ConfigManager::config_struct_to_string<multi_resize_config_t>(const multi_resize_config_t &conf);
template std::string ConfigManager::config_struct_to_string<eis_config_t>(const eis_config_t &conf);
template std::string ConfigManager::config_struct_to_string<gyro_config_t>(const gyro_config_t &conf);
template std::string ConfigManager::config_struct_to_string<ldc_config_t>(const ldc_config_t &conf);
template std::string ConfigManager::config_struct_to_string<denoise_config_t>(const denoise_config_t &conf);
template std::string ConfigManager::config_struct_to_string<isp_t>(const isp_t &conf);
template std::string ConfigManager::config_struct_to_string<hailort_t>(const hailort_t &conf);
template std::string ConfigManager::config_struct_to_string<hdr_config_t>(const hdr_config_t &conf);
template std::string ConfigManager::config_struct_to_string<encoder_config_t>(const encoder_config_t &conf);
template std::string ConfigManager::config_struct_to_string<vsm_config_t>(const vsm_config_t &conf);
template std::string ConfigManager::config_struct_to_string<isp_config_files_t>(const isp_config_files_t &conf);
template std::string ConfigManager::config_struct_to_string<codec_config_t>(const codec_config_t &conf);
template std::string ConfigManager::config_struct_to_string<medialib_config_t>(const medialib_config_t &conf);
template std::string ConfigManager::config_struct_to_string<profile_config_t>(const profile_config_t &conf);
template std::string ConfigManager::config_struct_to_string<frontend_config_t>(const frontend_config_t &conf);

tl::expected<std::string, media_library_return> ConfigManager::parse_config(std::string config_string,
                                                                            std::string entry)
{
    return ConfigManager::ConfigManagerImpl::parse_config(config_string, entry);
}

EncoderType ConfigManager::get_encoder_type(const nlohmann::json &config_json)
{
    return ConfigManager::ConfigManagerImpl::get_encoder_type(config_json);
}

//------------------------ ConfigManagerImpl ------------------------

media_library_return ConfigManager::ConfigManagerImpl::validate_config(const nlohmann::json &user_config)
{
    config_manager_error_handler err;
    m_config_validator.validate(user_config, err); // validate the document
    if (err)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate given json against schema");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManager::ConfigManagerImpl::validate_config_string(const std::string &user_config_string)
{
    const nlohmann::json user_config_json = nlohmann::json::parse(user_config_string, nullptr, false);
    if (user_config_json.is_discarded())
    {
        try
        {
            // This will throw with the actual parse error
            [[maybe_unused]] auto ignored_result = nlohmann::json::parse(user_config_string);
        }
        catch (const nlohmann::json::parse_error &e)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to parse string as JSON: {} at position {}",
                                  e.what(), e.byte);
            LOGGER__MODULE__ERROR(MODULE_NAME, "The following string was not valid JSON: {}", user_config_string);
        }
        catch (const std::exception &e)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to parse string as JSON: {}", e.what());
        }
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
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to parse string as JSON");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Validate against internal schema
    media_library_return validation_status = validate_config(user_config_json);
    if (validation_status == MEDIA_LIBRARY_CONFIGURATION_ERROR)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to validate json against schema");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Convert JSON to struct
    try
    {
        if constexpr (std::is_same<TConf, encoder_config_t>::value)
        {
            switch (get_encoder_type(user_config_json))
            {
            case EncoderType::Jpeg:
                conf = user_config_json.get<jpeg_encoder_config_t>();
                break;
            case EncoderType::Hailo:
                conf = user_config_json.get<hailo_encoder_config_t>();
                break;
            case EncoderType::None:
                LOGGER__MODULE__ERROR(MODULE_NAME,
                                      "Config Manager failed to convert JSON to struct: No supported encoder found");
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
        }
        else
        {
            conf = user_config_json.get<TConf>();
        }
    }
    catch (const nlohmann::json::exception &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to convert JSON to struct: {}", e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

template <typename TConf> std::string ConfigManager::ConfigManagerImpl::config_struct_to_string(const TConf &conf)
{
    try
    {
        nlohmann::json j;
        if constexpr (std::is_same<TConf, encoder_config_t>::value)
        {
            std::visit([&j](auto &&arg) { j = arg; }, conf);
        }
        else
        {
            j = conf;
        }
        return j.dump();
    }
    catch (const nlohmann::json::exception &e)
    {
        LOGGER__ERROR("Config Manager failed to convert struct to JSON string: {}", e.what());
        return "";
    }
}

tl::expected<std::string, media_library_return> ConfigManager::ConfigManagerImpl::parse_config(
    std::string config_string, std::string entry)
{
    // Convert string to JSON
    const nlohmann::json user_config_json = nlohmann::json::parse(config_string, nullptr, false);
    if (user_config_json.is_discarded())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to parse string as JSON");
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    // Check that the key exists
    if (!user_config_json.contains(entry))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to find requested entry in JSON string");
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
    }

    // return as a string
    return user_config_json[entry].dump();
}

EncoderType ConfigManager::ConfigManagerImpl::get_encoder_type(const nlohmann::json &config_json)
{
    if (!config_json.contains("encoding"))
    {
        return EncoderType::None;
    }

    if (config_json["encoding"].contains("jpeg_encoder"))
    {
        return EncoderType::Jpeg;
    }

    if (config_json["encoding"].contains("hailo_encoder"))
    {
        return EncoderType::Hailo;
    }

    return EncoderType::None;
}
