#include <filesystem>
#include <algorithm>
#include <initializer_list>
#include <optional>

#include "json_flattener.hpp"
#include "json_ref_utils.hpp"
#include "media_library_logger.hpp"
#include "files_utils.hpp"
#include "config_validator.hpp"

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
    ConfigParser config_parser(schema);
    media_library_return validation_status = config_parser.validate_configuration(json.dump());
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
    auto file_content_opt = files_utils::read_string_from_file(path);
    if (!file_content_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to read file: {}", path);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    try
    {
        content = nlohmann::json::parse(file_content_opt.value());
    }
    catch (const nlohmann::json::parse_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse json file: {}. Error: {}", path, e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return JsonParser::flatten_path(const std::string &path, nlohmann::json &output_json)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Starting to flatten JSON path: {}", path);
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

media_library_return JsonParser::flatten_json(const nlohmann::json &input_json, nlohmann::json &output_json,
                                              bool validate_schema, bool validate_metadata,
                                              const std::filesystem::path &base_dir)
{
    for (auto it = input_json.begin(); it != input_json.end(); ++it)
    {
        const std::string key = it.key();
        const auto &value = it.value();
        LOGGER__MODULE__TRACE(MODULE_NAME, "Processing key: {}", key);

        if (value.is_string() && is_path_to_json(value.get<std::string>()) &&
            (std::find(m_keys_to_not_flatten.begin(), m_keys_to_not_flatten.end(), key) == m_keys_to_not_flatten.end()))
        {
            const std::string resolved_path = json_ref_utils::anchor_path(value.get<std::string>(), base_dir);
            if (!std::filesystem::exists(resolved_path))
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Path does not exist: {}", resolved_path);
                return MEDIA_LIBRARY_CONFIGURATION_ERROR;
            }
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Found JSON path reference for key '{}': {}", key, resolved_path);
            nlohmann::json content;
            media_library_return status = flatten_path(resolved_path, content);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                return status;
            }
            nlohmann::json nested_output_json = content;
            const std::filesystem::path nested_base_dir = std::filesystem::path(resolved_path).parent_path();
            status = flatten_json(content, nested_output_json, validate_schema, validate_metadata, nested_base_dir);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                return status;
            }
            if (validate_schema)
            {
                status = schema_validate(key, nested_output_json);
                if (status != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Schema validation failed for key: {}", key);
                    return status;
                }
            }
            if (validate_metadata)
            {
                ConfigValidator config_validator;
                status = config_validator.validate_meta_data(key, content);
                if (status != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__MODULE__ERROR(MODULE_NAME, "Meta data validation failed for key: {}", key);
                    return status;
                }
            }
            output_json[add_suffix(key)] = nested_output_json;
            output_json[key] = resolved_path;
        }
        else if (value.is_string() && key == "sensor_calib_path")
        {
            output_json[key] = json_ref_utils::anchor_path(value.get<std::string>(), base_dir);
        }
        else if (value.is_object())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Processing nested object for key: {}", key);
            nlohmann::json nested_output = value;
            auto status = flatten_json(value, nested_output, validate_schema, validate_metadata, base_dir);
            if (status != MEDIA_LIBRARY_SUCCESS)
            {
                return status;
            }
            output_json[key] = nested_output;
        }
        else if (value.is_array())
        {
            LOGGER__MODULE__DEBUG(MODULE_NAME, "Processing array for key: {} with {} elements", key, value.size());
            // Handle arrays by flattening each element
            nlohmann::json array_output = nlohmann::json::array();
            for (const auto &element : value)
            {
                if (!element.is_object())
                {
                    array_output.push_back(element);
                    continue;
                }
                nlohmann::json nested_output = element;
                auto status = flatten_json(element, nested_output, validate_schema, validate_metadata, base_dir);
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
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return JsonParser::flatten_profile(const nlohmann::json &input_json, nlohmann::json &output_json,
                                                 bool validate_schema, bool validate_metadata,
                                                 const std::filesystem::path &base_dir)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Starting profile flattening. Schema validation: {}",
                          validate_schema ? "enabled" : "disabled");

    if (base_dir.empty() && json_ref_utils::has_relative_refs(input_json))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME,
                              "Profile JSON contains relative path references but MediaLibrary::initialize() was "
                              "called with a JSON object/string instead of a config file path. Pass the config "
                              "file path to initialize() so relative refs can be anchored at the file's directory, "
                              "or resolve all relative refs to absolute paths before calling initialize().");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

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
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Validating input JSON against root profile schema");
        // Validate the input JSON against the root schema
        media_library_return status = schema_validate(input_json, m_root_profile_schema);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Schema validation failed for input JSON");
            return status;
        }
        LOGGER__MODULE__TRACE(MODULE_NAME, "Root profile schema validation successful");
    }
    if (validate_metadata)
    {
        ConfigValidator config_validator;
        auto status = config_validator.validate_meta_data(input_json);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Meta data validation failed for input JSON");
            return status;
        }
    }
    output_json = input_json;
    return flatten_json(input_json, output_json, validate_schema, validate_metadata, base_dir);
}

media_library_return JsonParser::flatten_profile(const std::string &input_json_path, nlohmann::json &output_json,
                                                 bool validate_schema, bool validate_metadata)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Starting profile flattening from file: {}", input_json_path);
    nlohmann::json input_json;
    media_library_return ret = parse_path(input_json_path, input_json);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to parse input JSON from path: {}", input_json_path);
        return ret;
    }
    return flatten_profile(input_json, output_json, validate_schema, validate_metadata,
                           std::filesystem::path(input_json_path).parent_path());
}
