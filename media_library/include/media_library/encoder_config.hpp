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
#pragma once
#include <iostream>
#include <memory>
#include <vector>

// Open source includes
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>
#include "config_manager.hpp"

enum codec_t
{
    H264,
    H265
};

struct input_config_t
{
    uint32_t width;
    uint32_t height;
    std::string format;
    std::string framerate;
};

struct output_config_t
{
    codec_t codec;
    std::string profile;
    std::string level;
};

class EncoderConfig
{
private:
    std::shared_ptr<ConfigManager> m_config_manager;
    std::string m_json_string;
    nlohmann::json m_doc;
    // SchemaDocument m_schema_doc;
    // SchemaValidator m_validator;
public:
    EncoderConfig(const std::string &config_path);
    const nlohmann::json &get_doc() const;
    const nlohmann::json &get_input_stream() const;
    const input_config_t &get_input_config() const;
    const output_config_t &get_output_config() const;
    const nlohmann::json &get_gop_config() const;
    const nlohmann::json &get_output_stream() const;
    const nlohmann::json &get_coding_control() const;
    const nlohmann::json &get_rate_control() const;
};
