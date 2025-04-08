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
    media_library_return configure_medialib(std::string medialib_json_config_string);
    media_library_return set_profile(std::string profile);
    media_library_return set_profile(ProfileConfig profile);
    frontend_config_t get_frontend_config();
    std::string get_frontend_config_as_string();
    std::map<output_stream_id_t, encoder_config_t> get_encoder_configs();
    std::string get_3a_config();
    std::string get_sensor_entry();

    // media_library_return dump_active_configuration_to_file(string output_profile_path);

    MediaLibraryConfig m_medialib_config;
    ProfileConfig m_current_profile;

  private:
    ConfigManager m_medialib_config_manager;
    ConfigManager m_profile_config_manager;
    ConfigManager m_frontend_config_manager;
    ConfigManager m_encoder_config_manager;

    std::string m_medialib_json_config_string;
};
