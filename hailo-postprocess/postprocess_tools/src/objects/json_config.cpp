/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "hailo_postprocess_tools/objects/json_config.hpp"
#include "hailo_postprocess_tools/logger/hailo_postprocess_logger.hpp"

namespace common
{

bool validate_json_with_schema(rapidjson::FileReadStream stream, const char *json_schema)
{
    rapidjson::Document d;
    d.Parse(json_schema);
    rapidjson::SchemaDocument sd(d);
    rapidjson::SchemaValidator validator(sd);
    rapidjson::Document doc_config_json;
    rapidjson::Reader reader;
    if (!reader.Parse(stream, validator) && reader.GetParseErrorCode() != rapidjson::kParseErrorTermination)
    {
        HAILO_POSTPROCESS_LOG_ERROR("JSON error (offset {}): {}", static_cast<unsigned>(reader.GetErrorOffset()),
                                    GetParseError_En(reader.GetParseErrorCode()));
        throw std::runtime_error("Input is not a valid JSON");
    }

    if (validator.IsValid())
    {
        return true;
    }
    else
    {
        HAILO_POSTPROCESS_LOG_ERROR("Input JSON is invalid");
        rapidjson::StringBuffer sb;
        validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
        HAILO_POSTPROCESS_LOG_ERROR("Invalid schema: {}", sb.GetString());
        HAILO_POSTPROCESS_LOG_ERROR("Invalid keyword: {}", validator.GetInvalidSchemaKeyword());
        sb.Clear();
        validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
        HAILO_POSTPROCESS_LOG_ERROR("Invalid document: {}", sb.GetString());
        throw std::runtime_error("json config file doesn't follow schema rules");
    }
    return false;
}

} // namespace common
