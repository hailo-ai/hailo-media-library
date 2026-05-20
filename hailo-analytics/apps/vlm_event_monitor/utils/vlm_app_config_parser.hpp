#pragma once

#include <string>

#include "vlm_app_config.hpp"

namespace vlm_app_config
{

class VlmAppConfigParser
{
  public:
    VlmAppConfigParser() = default;

    // Parse a YAML config file from disk. Missing optional fields fall back to
    // the defaults in VlmAppConfig. Returns false on file/IO errors or on a
    // structural problem with the YAML.
    bool parse_from_file(const std::string &path);

    // Parse a YAML string in memory (used for tests / overrides).
    bool parse_from_string(const std::string &yaml_content);

    const VlmAppConfig &get_config() const
    {
        return m_config;
    }

  private:
    VlmAppConfig m_config;
};

} // namespace vlm_app_config
