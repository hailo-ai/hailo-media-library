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
/**
 * @file config_manager.hpp
 * @brief MediaLibrary Configuration loading and processing
 **/

#pragma once
#include <stdint.h>
#include <string>
#include <tl/expected.hpp>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

#include "media_library_types.hpp"

enum ConfigSchema
{
    CONFIG_SCHEMA_OSD,
    CONFIG_SCHEMA_PRIVACY_MASK,
    CONFIG_SCHEMA_ENCODER_AND_BLENDING,
    CONFIG_SCHEMA_ENCODER,
    CONFIG_SCHEMA_MULTI_RESIZE,
    CONFIG_SCHEMA_LDC,
    CONFIG_SCHEMA_VSM,
    CONFIG_SCHEMA_HAILORT,
    CONFIG_SCHEMA_ISP,
    CONFIG_SCHEMA_HDR,
    CONFIG_SCHEMA_DENOISE,
    CONFIG_SCHEMA_INPUT_VIDEO,
    CONFIG_SCHEMA_FRONTEND,
    CONFIG_SCHEMA_MEDIALIB_CONFIG,
    CONFIG_SCHEMA_PROFILE,
    CONFIG_SCHEMA_APPLICATION_ANALYTICS,
    CONFIG_SCHEMA_NONE,
    CONFIG_SCHEMA_SENSOR_CONFIG,
    CONFIG_SCHEMA_APPLICATION_SETTINGS,
    CONFIG_SCHEMA_STABILIZER_SETTINGS,
    CONFIG_SCHEMA_AUTOMATIC_ALGORITHMS,
    CONFIG_SCHEMA_IQ_SETTINGS,
};

class ConfigManager
{
  public:
    /**
     * @brief Constructor for the ConfigManager module
     *
     */
    ConfigManager(ConfigSchema schema);

    /**
     * @brief Destructor for the ConfigManager module
     */
    ~ConfigManager();

    /**
     * @brief Copy constructor (deleted)
     */
    ConfigManager(const ConfigManager &) = delete;

    /**
     * @brief Copy assignment operator (deleted)
     */
    ConfigManager &operator=(const ConfigManager &) = delete;

    /**
     * @brief Move constructor
     */
    ConfigManager(ConfigManager &&) = delete;

    /**
     * @brief Move assignment
     */
    ConfigManager &operator=(ConfigManager &&) = delete;

    /**
     * @brief Validate the user's configuration json string against internal
     * schema
     *
     * @param[in] user_config_string - the user's configuration (as a json
     * string)
     * @return media_library_return
     */
    media_library_return validate_configuration(const std::string &user_config_string);

    /**
     * @brief Check if the user's configuration json string is valid
     *
     * @param[in] user_config_string - the user's configuration (as a json
     * string)
     * @return true if valid, false otherwise
     */
    bool is_valid_configuration(const std::string &user_config_string);

    /**
     * @brief Validate a json string and populate a configuration struct
     *
     * @param[in] user_config - the user's configuration (as a json string)
     * @param[out] conf - the user's configuration (as a json string)
     * @return media_library_return
     */
    template <typename TConf>
    media_library_return config_string_to_struct(const std::string &user_config_string, TConf &conf);

    /**
     * @brief Convert a configuration struct to a json string
     *
     * @param[in] conf - the configuration struct
     * @param[in] spaces - number of spaces for JSON indentation (default: 0)
     * @return std::string
     */
    template <typename TConf> std::string config_struct_to_string(const TConf &conf, int spaces = -1);

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
    class ConfigManagerImpl; // internal implementation class
    std::unique_ptr<ConfigManagerImpl> m_config_manager_impl;
};
