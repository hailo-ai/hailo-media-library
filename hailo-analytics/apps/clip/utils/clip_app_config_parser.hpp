#pragma once

#include <string>

#include "clip_app_config.hpp"
#include "rapidyaml-0.9.0.hpp"

class ClipAppConfigParser
{
  private:
    ClipAppConfig m_config;

    // Helper method to safely get string value from YAML node
    std::string get_string_value(const ryml::ConstNodeRef &node, const std::string &key,
                                 const std::string &defaultValue = "");

    // Helper method to safely get float value from YAML node
    float get_float_value(const ryml::ConstNodeRef &node, const std::string &key, float defaultValue = 0.0f);

    // Helper method to safely get integer value from YAML node
    int get_int_value(const ryml::ConstNodeRef &node, const std::string &key, int defaultValue = 0);

    // Helper method to safely get boolean value from YAML node
    bool get_bool_value(const ryml::ConstNodeRef &node, const std::string &key, bool defaultValue = false);

    // Parse network configurations
    void parse_network_configs(const ryml::ConstNodeRef &root);

    // Parse HailoRT device configuration
    void parse_hailort_device_config(const ryml::ConstNodeRef &root);

    // Parse storage configuration
    void parse_storage_config(const ryml::ConstNodeRef &root);

    // Parse server configurations
    void parse_server_config(const ryml::ConstNodeRef &root);

    // Parse image encoders configuration
    void parse_image_encoders_config(const ryml::ConstNodeRef &root);

    // Helper method to parse FAISS index configuration
    void parse_faiss_config(const ryml::ConstNodeRef &root);

    void parse_pipeline_config(const ryml::ConstNodeRef &root);

    void parse_frontend_source_from_file_config(const ryml::ConstNodeRef &root);

    void parse_query_defaults_config(const ryml::ConstNodeRef &root);

  public:
    // Constructor
    ClipAppConfigParser() = default;

    // Parse configuration from YAML file
    bool parse_from_file(const std::string &configPath);

    // Parse configuration from YAML string
    bool parse_from_string(const std::string &yamlContent);

    // Get the parsed configuration
    const ClipAppConfig &get_config() const;

    // Get mutable reference to config (for runtime modifications)
    ClipAppConfig &get_config();
};
