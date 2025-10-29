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

class config_manager_error_handler_does_not_throw : public nlohmann::json_schema::basic_error_handler
{
  public:
    bool valid = true;
    void error(const nlohmann::json_pointer<nlohmann::basic_json<>> &pointer, const nlohmann::json &instance,
               const std::string &message) override
    {
        (void)pointer;
        (void)instance;
        (void)message;
        valid = false;
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
        case ConfigSchema::CONFIG_SCHEMA_ENCODER_AND_BLENDING:
            m_config_validator.set_root_schema(config_schemas::encoder_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_ENCODER:
            m_config_validator.set_root_schema(config_schemas::encoding_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_MULTI_RESIZE:
            m_config_validator.set_root_schema(config_schemas::multi_resize_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_OSD:
            m_config_validator.set_root_schema(config_schemas::osd_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK:
            m_config_validator.set_root_schema(config_schemas::privacy_mask_config_schema);
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
        case ConfigSchema::CONFIG_SCHEMA_HDR:
            m_config_validator.set_root_schema(config_schemas::hdr_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_DENOISE:
            m_config_validator.set_root_schema(config_schemas::denoise_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_INPUT_VIDEO:
            m_config_validator.set_root_schema(config_schemas::input_video_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_APPLICATION_ANALYTICS:
            m_config_validator.set_root_schema(config_schemas::application_analytics_config_schema);
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
        case ConfigSchema::CONFIG_SCHEMA_NONE:
            m_config_validator.set_root_schema(config_schemas::empty_config_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_IQ_SETTINGS:
            m_config_validator.set_root_schema(config_schemas::iq_settings_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_STABILIZER_SETTINGS:
            m_config_validator.set_root_schema(config_schemas::stebilizer_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_APPLICATION_SETTINGS:
            m_config_validator.set_root_schema(config_schemas::application_settings_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_SENSOR_CONFIG:
            m_config_validator.set_root_schema(config_schemas::sensor_config_file_schema);
            break;
        case ConfigSchema::CONFIG_SCHEMA_AUTOMATIC_ALGORITHMS:
            m_config_validator.set_root_schema(config_schemas::automatic_algorithms_schema);
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
     * @brief Validate the user's configuration json string against internal schema
     *
     * @param[in] user_config_string - the user's configuration (as a json string)
     * @return bool - true if the configuration string is valid, false otherwise
     */
    bool is_valid_configuration(const std::string &user_config_string);

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
    template <typename TConf> std::string config_struct_to_string(const TConf &conf, int spaces = 0);

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

    /**
     * @brief Retrieve the input source element type from a frontend configuration
     *
     * @param[in] cfg - the user's frontend configuration (as a struct)
     * @return frontend_src_element_t
     */
    static frontend_src_element_t get_input_stream_type(const frontend_config_t &cfg);

    /**
     * @brief Retrieve the input resolution (width, height) from a frontend configuration
     *
     * @param[in] cfg - the user's frontend configuration (as a struct)
     * @return std::pair<uint16_t, uint16_t> as {width, height} in pixels
     */
    static std::pair<uint16_t, uint16_t> get_input_resolution(const frontend_config_t &cfg);

    /**
     * @brief Check whether changing from old_config to new_config is allowed without rebuilding the pipeline.
     *
     * @param[in] old_config The previously applied frontend configuration.
     * @param[in] new_config The candidate frontend configuration to switch to.
     * @return true if the change is allowed (i.e., only framerate may differ); false otherwise.
     */
    static bool is_config_change_allowed(const frontend_config_t &old_config, const frontend_config_t &new_config);

  private:
    nlohmann::json_schema::json_validator m_config_validator;

    /*
     * @brief Validate the user's configuration json against internal schema
     *
     * @param[in] user_config - the user's configuration
     * @param[in] treat_invalid_as_error - if true, treat invalid configurations as errors
     * @return media_library_return
     */
    media_library_return validate_config(const nlohmann::json &user_config, bool treat_invalid_as_error = true);
    /**
     * @brief Validate a configuration string and populate a configuration struct
     *
     * @param[in] user_config_string - the user's configuration (as a json string)
     * @param[out] conf - the user's configuration (as a json string)
     * @param treat_invalid_as_error - if true, treat invalid configurations as errors
     * @return media_library_return
     */
    media_library_return validate_config_string(const std::string &user_config_string, bool treat_invalid_as_error);
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

bool ConfigManager::is_valid_configuration(const std::string &user_config_string)
{
    return m_config_manager_impl->is_valid_configuration(user_config_string);
}

template <typename TConf>
media_library_return ConfigManager::config_string_to_struct(const std::string &user_config_string, TConf &conf)
{
    return m_config_manager_impl->config_string_to_struct<TConf>(user_config_string, conf);
}

template <typename TConf> std::string ConfigManager::config_struct_to_string(const TConf &conf, int spaces)
{
    return m_config_manager_impl->config_struct_to_string(conf, spaces);
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
template media_library_return ConfigManager::config_string_to_struct<application_analytics_config_t>(
    const std::string &user_config_string, application_analytics_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<frontend_config_t>(
    const std::string &user_config_string, frontend_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<medialib_config_t>(
    const std::string &user_config_string, medialib_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<automatic_algorithms_config_t>(
    const std::string &user_config_string, automatic_algorithms_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<privacy_mask_config_t>(
    const std::string &user_config_string, privacy_mask_config_t &conf);
template media_library_return ConfigManager::config_string_to_struct<config_profile_t>(
    const std::string &user_config_string, config_profile_t &conf);

template std::string ConfigManager::config_struct_to_string<input_video_config_t>(const input_video_config_t &conf,
                                                                                  int spaces);
template std::string ConfigManager::config_struct_to_string<multi_resize_config_t>(const multi_resize_config_t &conf,
                                                                                   int spaces);
template std::string ConfigManager::config_struct_to_string<eis_config_t>(const eis_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<gyro_config_t>(const gyro_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<ldc_config_t>(const ldc_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<denoise_config_t>(const denoise_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<isp_t>(const isp_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<hailort_t>(const hailort_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<hdr_config_t>(const hdr_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<encoder_config_t>(const encoder_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<vsm_config_t>(const vsm_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<codec_config_t>(const codec_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<application_analytics_config_t>(
    const application_analytics_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<medialib_config_t>(const medialib_config_t &conf,
                                                                               int spaces);
template std::string ConfigManager::config_struct_to_string<frontend_config_t>(const frontend_config_t &conf,
                                                                               int spaces);
template std::string ConfigManager::config_struct_to_string<automatic_algorithms_config_t>(
    const automatic_algorithms_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<isp_format_aaa_config_t>(
    const isp_format_aaa_config_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<privacy_mask_config_t>(const privacy_mask_config_t &conf,
                                                                                   int spaces);
template std::string ConfigManager::config_struct_to_string<config_sensor_configuration_t>(
    const config_sensor_configuration_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<config_stream_osd_t>(const config_stream_osd_t &conf,
                                                                                 int spaces);
template std::string ConfigManager::config_struct_to_string<isp_format_config_sensor_configuration_t>(
    const isp_format_config_sensor_configuration_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<config_encoded_output_stream_t>(
    const config_encoded_output_stream_t &conf, int spaces);
template std::string ConfigManager::config_struct_to_string<config_profile_t>(const config_profile_t &conf, int spaces);

tl::expected<std::string, media_library_return> ConfigManager::parse_config(std::string config_string,
                                                                            std::string entry)
{
    return ConfigManager::ConfigManagerImpl::parse_config(config_string, entry);
}

EncoderType ConfigManager::get_encoder_type(const nlohmann::json &config_json)
{
    return ConfigManager::ConfigManagerImpl::get_encoder_type(config_json);
}

frontend_src_element_t ConfigManager::get_input_stream_type(const frontend_config_t &config)
{
    return ConfigManager::ConfigManagerImpl::get_input_stream_type(config);
}

std::pair<uint16_t, uint16_t> ConfigManager::get_input_resolution(const frontend_config_t &config)
{
    return ConfigManager::ConfigManagerImpl::get_input_resolution(config);
}

bool ConfigManager::is_config_change_allowed(const frontend_config_t &old_config, const frontend_config_t &new_config)
{
    return ConfigManager::ConfigManagerImpl::is_config_change_allowed(old_config, new_config);
}

//------------------------ ConfigManagerImpl ------------------------

media_library_return ConfigManager::ConfigManagerImpl::validate_config(const nlohmann::json &user_config,
                                                                       bool treat_invalid_as_error)
{
    if (treat_invalid_as_error)
    {
        config_manager_error_handler err;
        m_config_validator.validate(user_config, err);
        if (err)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to validate given json against schema");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        return MEDIA_LIBRARY_SUCCESS;
    }
    config_manager_error_handler_does_not_throw err_f;
    m_config_validator.validate(user_config, err_f);
    return err_f.valid ? MEDIA_LIBRARY_SUCCESS : MEDIA_LIBRARY_CONFIGURATION_ERROR;
}

bool ConfigManager::ConfigManagerImpl::is_valid_configuration(const std::string &user_config_string)
{
    return validate_config_string(user_config_string, false) == MEDIA_LIBRARY_SUCCESS;
}

media_library_return ConfigManager::ConfigManagerImpl::validate_config_string(const std::string &user_config_string)
{
    return validate_config_string(user_config_string, true);
}

media_library_return ConfigManager::ConfigManagerImpl::validate_config_string(const std::string &user_config_string,
                                                                              bool treat_invalid_as_error)
{
    const nlohmann::json user_config_json = nlohmann::json::parse(user_config_string, nullptr, false);
    if (user_config_json.is_discarded())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to parse string as JSON");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    media_library_return ret = validate_config(user_config_json, treat_invalid_as_error);
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
        // Enhanced error logging with JSON content and type information
        std::string config_type_name = typeid(TConf).name();
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to convert JSON to struct of type '{}': {}\n",
                              config_type_name, e.what());

        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

template <typename TConf>
std::string ConfigManager::ConfigManagerImpl::config_struct_to_string(const TConf &conf, int spaces)
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
        if (spaces == 0)
        {
            return j.dump();
        }
        return j.dump(spaces);
    }
    catch (const nlohmann::json::exception &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config Manager failed to convert struct to JSON string: {}", e.what());
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

frontend_src_element_t ConfigManager::ConfigManagerImpl::get_input_stream_type(const frontend_config_t &cfg)
{
    return cfg.input_config.source_type;
}

std::pair<uint16_t, uint16_t> ConfigManager::ConfigManagerImpl::get_input_resolution(const frontend_config_t &cfg)
{
    const auto &dims = cfg.input_config.resolution.dimensions;
    return {static_cast<uint16_t>(dims.destination_width), static_cast<uint16_t>(dims.destination_height)};
}

bool ConfigManager::ConfigManagerImpl::is_config_change_allowed(const frontend_config_t &old_config,
                                                                const frontend_config_t &new_config)
{
    if (ConfigManager::get_input_stream_type(old_config) != ConfigManager::get_input_stream_type(new_config))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config change not allowed, input stream type is different");
        return false;
    }

    const auto &old_res = old_config.multi_resize_config.application_input_streams_config.resolutions;
    const auto &new_res = new_config.multi_resize_config.application_input_streams_config.resolutions;

    if (old_res.size() != new_res.size())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Config change not allowed, number of output streams is different");
        return false;
    }

    for (size_t i = 0; i < old_res.size(); ++i)
    {
        const uint16_t old_w = static_cast<uint16_t>(old_res[i].dimensions.destination_width);
        const uint16_t old_h = static_cast<uint16_t>(old_res[i].dimensions.destination_height);
        const uint16_t new_w = static_cast<uint16_t>(new_res[i].dimensions.destination_width);
        const uint16_t new_h = static_cast<uint16_t>(new_res[i].dimensions.destination_height);

        // Ignore framerate differences; enforce all other compared fields equal (order-aware)
        if (old_w != new_w || old_h != new_h)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Config change not allowed, output streams are different");
            return false;
        }
    }

    return true; // FPS may change
}
