#pragma once
#include <mutex>
#include "media_library/media_library.hpp"
#include "media_library/media_library_api_types.hpp"
#include "common/logger_macros.hpp"

namespace webserver
{
namespace pipeline
{
class IspBlender
{
  public:
    IspBlender() : m_media_library(std::nullopt), pipeline_active(false)
    {
    }

    void set_media_library(std::shared_ptr<MediaLibrary> mediaLib);
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
    std::optional<std::shared_ptr<MediaLibrary>> m_media_library;
    bool pipeline_active;
};
} // namespace pipeline
} // namespace webserver
