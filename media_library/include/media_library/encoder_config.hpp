#pragma once
#include <iostream>
#include <memory>
#include <vector>

// Open source includes
#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/schema.h"
#include "rapidjson/prettywriter.h"

using namespace rapidjson;

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
    std::string m_json_string;
    Document m_doc;
    // SchemaDocument m_schema_doc;
    // SchemaValidator m_validator;
public:
    EncoderConfig(const std::string& config_path);
    const Document& get_doc() const;
    const Value::ConstObject get_input_stream() const;
    const input_config_t & get_input_config() const;
    const output_config_t & get_output_config() const;
    const Value::ConstObject get_gop_config() const;
    const Value::ConstObject get_output_stream() const;
    const Value::ConstObject get_coding_control() const;
    const Value::ConstObject get_rate_control() const;
};

