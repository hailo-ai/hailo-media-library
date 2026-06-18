#pragma once

#include <tl/expected.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <utility>
#include <memory>

#include "media_library_types.hpp"
#include "config_manager.hpp"
#include "config_parser.hpp"

class ConfigBackup
{
  public:
    /**
     * @brief Backup all profiles from a MediaLibraryConfig to a folder
     *
     * @param[in] hml_config - The media library configuration containing profiles to backup
     * @param[in] backup_folder_path - The folder path where backups should be stored
     * @return media_library_return - MEDIA_LIBRARY_SUCCESS on success, error code otherwise
     */
    static media_library_return backup_profiles(const MediaLibraryConfig &hml_config,
                                                const std::string &backup_folder_path);

  private:
    static media_library_return write_config_json_to_file(nlohmann::json config_json, const std::string &dest_path);

    template <typename ConfigType>
    static media_library_return backup_config_section(ConfigSchema schema, const ConfigType &config_section,
                                                      const std::string &config_path,
                                                      const std::string &wrapper_key = "",
                                                      bool unwrap_if_wrapped = false);

    static tl::expected<nlohmann::json, media_library_return> backup_encoded_output_streams(
        const std::shared_ptr<config_profile_t> &profile_config, const std::string &profile_folder);
    static tl::expected<std::string, media_library_return> backup_single_profile(
        const std::string &profile_name, const std::shared_ptr<config_profile_t> &profile_config,
        const std::string &backup_folder_path);
    static media_library_return backup_medialib_config(
        const MediaLibraryConfig &hml_config, const std::string &backup_folder_path,
        const std::vector<std::pair<std::string, std::string>> &backed_up_profiles);
};
