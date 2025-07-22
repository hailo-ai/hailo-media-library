#pragma once

#include <iostream>
#include <string>
#include <vector>
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/media_library_api_types.hpp"
#include "media_library/config_manager.hpp"

class MediaLibConfigManager
{
  public:
    MediaLibConfigManager();
    ~MediaLibConfigManager();
    media_library_return validate_configuration(std::string config_string, ConfigSchema config_schema_type);
    bool is_valid_configuration(const std::string &config_string, ConfigSchema config_schema_type);
    media_library_return configure_medialib(std::string medialib_json_config_string);
    media_library_return set_profile(std::string profile);
    media_library_return set_profile(ProfileConfig profile);
    ProfileConfig set_frontend_config(frontend_config_t frontend_config);
    ProfileConfig get_profile(const std::string &profile_name);
    ProfileConfig get_default_profile();
    frontend_config_t get_frontend_config();
    std::string get_frontend_config_as_string();
    std::map<output_stream_id_t, encoder_config_t> get_encoder_configs();
    std::string get_3a_config();
    std::string get_sensor_entry();
    std::string get_3a_config_as_string();
    MediaLibraryConfig m_medialib_config;
    ProfileConfig m_current_profile;
    restricted_profile_type_t m_restricted_profile_type;

  private:
    ConfigManager m_medialib_config_manager;
    ConfigManager m_profile_config_manager;
    ConfigManager m_frontend_config_manager;
    ConfigManager m_encoder_config_manager;

    std::string m_medialib_json_config_string;
};
