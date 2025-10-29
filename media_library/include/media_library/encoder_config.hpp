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
#pragma once
#include <iostream>
#include <memory>
#include <vector>
#include <variant>

// Open source includes
#include "config_manager.hpp"
#include "encoder_config_types.hpp"
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

class EncoderConfig
{
  private:
    EncoderType type;
    std::shared_ptr<ConfigManager> m_config_manager;
    std::string m_json_string;
    nlohmann::json m_doc;
    encoder_config_t m_config;
    encoder_config_t m_user_config;
    struct input_config_t m_input_config;
    struct output_config_t m_output_config;

  public:
    EncoderConfig(const std::string &config_path);
    const nlohmann::json &get_doc() const;
    media_library_return configure(const std::string &config_path);
    media_library_return configure(const encoder_config_t &encoder_config);
    encoder_config_t get_config();
    encoder_config_t get_user_config();
    hailo_encoder_config_t get_hailo_config();
    jpeg_encoder_config_t get_jpeg_config();

    /**
     * @brief Compare two encoder_config_t structures for equality
     *
     * This method performs a deep comparison of all fields in both encoder config structures,
     * including all nested structures and optional fields.
     *
     * @param old_config The first encoder configuration to compare
     * @param new_config The second encoder configuration to compare
     * @return true if all values in both structures are identical, false otherwise
     */
    static bool config_struct_equal(const encoder_config_t &old_config, const encoder_config_t &new_config);
};
