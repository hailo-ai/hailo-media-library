#pragma once
#include <mutex>
#include "media_library/media_library.hpp"

namespace webserver
{
namespace pipeline
{
class IspBlender
{
  public:
    IspBlender();

    void set_media_library(MediaLibrary &mediaLib);
    void unset_media_library();
    automatic_algorithms_config_t get_current_automatic_algorithms_config();
    void set_automatic_algorithms_config(const automatic_algorithms_config_t &config);
    void set_auto_configs(bool enabled);
    void reset_auto_configs();
    config_profile_t get_profile_by_name(const std::string &profile_name);
    bool is_pipeline_active() const;

  private:
    config_profile_t get_current_profile();
    void applyProfile(const config_profile_t &cfg) const;

    mutable std::recursive_mutex mutex;
    std::optional<MediaLibrary *> m_media_library;
    bool pipeline_active;
};
} // namespace pipeline
} // namespace webserver
