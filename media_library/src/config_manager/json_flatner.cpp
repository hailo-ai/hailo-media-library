#include <filesystem>
#include <fstream>
#include "json_flatner.hpp"
#include "media_library_logger.hpp"
#include <iostream>

#define MODULE_NAME LoggerType::Config

const std::string JsonParser::content_suffix = "_content";

media_library_return JsonParser::schema_validate(const std::string &key,
                                                 const nlohmann::json &value_coresponding_to_key)
{
    if (m_keys_and_coresponding_schema.find(key) == m_keys_and_coresponding_schema.end())
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Key {} not found in schema map", key);
        return MEDIA_LIBRARY_SUCCESS;
    }
    ConfigSchema schema = m_keys_and_coresponding_schema[key];
    return schema_validate(value_coresponding_to_key, schema);
}

media_library_return JsonParser::schema_validate(const nlohmann::json &json, const ConfigSchema &schema)
{
    ConfigManager config_manager(schema);
    media_library_return validation_status = config_manager.validate_configuration(json.dump());
    if (validation_status != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Schema validation failed for json: {} and schema: {}", json.dump(), schema);
        return validation_status;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return JsonParser::parse_path(const std::string path, nlohmann::json &content)
{
    std::filesystem::path p = path;
    if (!std::filesystem::exists(p))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Path does not exist: {}", path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (p.extension() != ".json")
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Path is not a json file: {}", path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    std::ifstream file(p);
    if (!file.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file: {}", path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    try
    {
        file >> content;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse json file: {}. Error: {}", path, e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    file.close();
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return JsonParser::flatten_path(const std::string &path, nlohmann::json &output_json)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting to flatten JSON path: {}", path);
    nlohmann::json content;
    media_library_return status = parse_path(path, content);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        return status;
    }
    if (content.is_null())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Content is null for path: {}", path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (!content.is_object())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Content is not a valid json object for path: {}", path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    output_json = content;
    return MEDIA_LIBRARY_SUCCESS;
}

bool is_path_to_json(const std::string &path)
{
    std::filesystem::path p = path;
    return p.has_filename() && p.extension() == ".json";
}

media_library_return JsonParser::flatten_json(const nlohmann::json &input_json, nlohmann::json &output_json)
{
    int processed_keys = 0;

    for (auto it = input_json.begin(); it != input_json.end(); ++it)
    {
        const std::string key = it.key();
        const auto &value = it.value();
        LOGGER__MODULE__TRACE(MODULE_NAME, "Processing key: {}", key);

        if (value.is_string() && is_path_to_json(value.get<std::string>()) &&
            (std::find(m_keys_to_not_flatten.begin(), m_keys_to_not_flatten.end(), key) == m_keys_to_not_flatten.end()))
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Found JSON path reference for key '{}': {}", key,
                                  value.get<std::string>());
            if (!std::filesystem::exists(value.get<std::string>()))
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Path does not exist: {}", value.get<std::string>());
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            nlohmann::json content;
            media_library_return status = flatten_path(value.get<std::string>(), content);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                return status;
            }
            nlohmann::json nested_output_json;
            nested_output_json = content;
            status = flatten_json(content, nested_output_json);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                return status;
            }
            status = schema_validate(key, nested_output_json);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Schema validation failed for key: {}", key);
                return status;
            }
            output_json[add_suffix(key)] = nested_output_json;
        }
        else if (value.is_object())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Processing nested object for key: {}", key);
            // Recursively flatten nested objects
            nlohmann::json nested_output;
            nested_output = value;
            auto status = flatten_json(value, nested_output);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                return status;
            }
        }
        else if (value.is_array())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Processing array for key: {} with {} elements", key, value.size());
            // Handle arrays by flattening each element
            nlohmann::json array_output;
            for (const auto &element : value)
            {
                if (!element.is_object())
                {
                    array_output.push_back(element);
                    continue;
                }
                nlohmann::json nested_output;
                nested_output = element;
                auto status = flatten_json(element, nested_output);
                if (status != MEDIA_LIBRARY_SUCCESS)
                {
                    return status;
                }
                array_output.push_back(nested_output);
            }
            output_json[key] = array_output;
        }
        else
        {
            LOGGER__MODULE__TRACE(MODULE_NAME, "Processing primitive value for key: {}", key);
            output_json[key] = value;
        }
        processed_keys++;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return JsonParser::flatten_profile(const nlohmann::json &input_json, nlohmann::json &output_json,
                                                 bool validate_schema)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting profile flattening. Schema validation: {}",
                         validate_schema ? "enabled" : "disabled");

    if (!output_json.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Output json have to be empty");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (input_json.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Input json is empty");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (!input_json.is_object())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Input json is not a valid json object");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (validate_schema)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Validating input JSON against root profile schema");
        // Validate the input JSON against the root schema
        media_library_return status = schema_validate(input_json, m_root_profile_schema);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Schema validation failed for input JSON");
            return status;
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "Root profile schema validation successful");
    }
    // Flatten the input JSON
    output_json = input_json;
    return flatten_json(input_json, output_json);
}

media_library_return JsonParser::flatten_profile(const std::string &input_json_path, nlohmann::json &output_json,
                                                 bool validate_schema)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting profile flattening from file: {}", input_json_path);
    nlohmann::json input_json;
    media_library_return ret = parse_path(input_json_path, input_json);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse input JSON from path: {}", input_json_path);
        return ret;
    }
    return flatten_profile(input_json, output_json, validate_schema);
}
