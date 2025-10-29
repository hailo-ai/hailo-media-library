#pragma once
#include <nlohmann/json-schema.hpp>
#include <string>
#include <unordered_map>
#include "media_library_types.hpp"
#include "config_manager.hpp"

class JsonParser
{
  private:
    static const std::string content_suffix;
    ConfigSchema m_root_profile_schema = ConfigSchema::CONFIG_SCHEMA_PROFILE;
    std::unordered_map<std::string, ConfigSchema> m_keys_and_coresponding_schema = {
        {"sensor_config", ConfigSchema::CONFIG_SCHEMA_SENSOR_CONFIG},
        {"application_settings", ConfigSchema::CONFIG_SCHEMA_APPLICATION_SETTINGS},
        {"stabilizer_settings", ConfigSchema::CONFIG_SCHEMA_STABILIZER_SETTINGS},
        {"iq_settings", ConfigSchema::CONFIG_SCHEMA_IQ_SETTINGS},
        {"encoding", ConfigSchema::CONFIG_SCHEMA_ENCODER},
        {"osd", ConfigSchema::CONFIG_SCHEMA_OSD},
        {"masking", ConfigSchema::CONFIG_SCHEMA_PRIVACY_MASK}};
    std::vector<std::string> m_keys_to_not_flatten = {"eis_config_path"};
    media_library_return parse_path(const std::string path, nlohmann::json &content);
    media_library_return flatten_path(const std::string &path, nlohmann::json &output_json);
    media_library_return flatten_json(const nlohmann::json &input_json, nlohmann::json &output_json);
    media_library_return schema_validate(const nlohmann::json &json, const ConfigSchema &schema);
    media_library_return schema_validate(const std::string &key, const nlohmann::json &value_coresponding_to_key);

  public:
    std::string add_suffix(const std::string &key) const
    {
        return key + content_suffix;
    }
    media_library_return flatten_profile(const nlohmann::json &input_json, nlohmann::json &output_json,
                                         bool validate_schema = true);
    media_library_return flatten_profile(const std::string &input_json_path, nlohmann::json &output_json,
                                         bool validate_schema = true);
};
