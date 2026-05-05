#include "isp_blender.hpp"
#include "common/logger_macros.hpp"

using namespace webserver::pipeline;
#define MEDIALIB_NOT_SET "Media library is not set"

IspBlender::IspBlender() : m_media_library(std::nullopt), pipeline_active(false)
{
}

void IspBlender::applyProfile(const config_profile_t &cfg) const
{
    if (!m_media_library.has_value())
    {
        WEBSERVER_LOG_ERROR(MEDIALIB_NOT_SET);
        throw std::runtime_error(MEDIALIB_NOT_SET);
    }
    if (m_media_library.value()->set_override_parameters(cfg) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        constexpr char ERROR_MESSAGE[] = "Failed to set profile";
        WEBSERVER_LOG_ERROR(ERROR_MESSAGE);
        throw std::runtime_error(ERROR_MESSAGE);
    }
}

config_profile_t IspBlender::get_current_profile()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    WEBSERVER_LOG_DEBUG("Pipeline: Handling update profile event");
    if (!m_media_library.has_value())
    {
        WEBSERVER_LOG_ERROR(MEDIALIB_NOT_SET);
        throw std::runtime_error(MEDIALIB_NOT_SET);
    }
    auto expected_profile = m_media_library.value()->get_current_profile();
    if (!expected_profile.has_value())
    {
        constexpr char ERROR_MESSAGE[] = "Failed to get current profile";
        WEBSERVER_LOG_ERROR(ERROR_MESSAGE);
        throw std::runtime_error(ERROR_MESSAGE);
    }
    config_profile_t current_profile = expected_profile.value();
    return current_profile;
}

automatic_algorithms_config_t IspBlender::get_current_automatic_algorithms_config()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    config_profile_t current_profile = get_current_profile();
    return current_profile.iq_settings.automatic_algorithms_config;
}

void IspBlender::set_automatic_algorithms_config(const automatic_algorithms_config_t &config)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    config_profile_t current_profile = get_current_profile();
    current_profile.iq_settings.automatic_algorithms_config = config;
    applyProfile(current_profile);
}

void IspBlender::set_auto_configs(bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    config_profile_t current_profile = get_current_profile();
    WEBSERVER_LOG_DEBUG("Setting automatic algorithms to {}", enabled);
    WEBSERVER_LOG_TRACE("Setting ae_e_v1 from {} to {}",
                        current_profile.iq_settings.automatic_algorithms_config.ae_e_v1.enabled, enabled);
    WEBSERVER_LOG_TRACE("Setting a_cproc from {} to {}",
                        current_profile.iq_settings.automatic_algorithms_config.a_cproc.enabled, enabled);
    WEBSERVER_LOG_TRACE("Setting aw_drv4 from {} to {}",
                        current_profile.iq_settings.automatic_algorithms_config.aw_drv4.enabled, enabled);

    automatic_algorithms_config_t &config = current_profile.iq_settings.automatic_algorithms_config;
    config.ae_e_v1.disable = false;
    config.a_cproc.disable = false;
    config.aw_drv4.disable = false;
    config.ae_e_v1.enabled = enabled;
    config.a_cproc.enabled = enabled;
    config.aw_drv4.enabled = enabled;
    applyProfile(current_profile);
}
void IspBlender::reset_auto_configs()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    config_profile_t current_profile = get_current_profile();

    std::string current_profile_name = current_profile.name;
    config_profile_t current_profile_original_state = get_profile_by_name(current_profile_name);
    current_profile.iq_settings.automatic_algorithms_config =
        current_profile_original_state.iq_settings.automatic_algorithms_config;
    applyProfile(current_profile);
}

config_profile_t IspBlender::get_profile_by_name(const std::string &profile_name)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!m_media_library.has_value())
    {
        WEBSERVER_LOG_ERROR(MEDIALIB_NOT_SET);
        throw std::runtime_error(MEDIALIB_NOT_SET);
    }
    auto expected_profile = m_media_library.value()->get_profile(profile_name);
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get profile by name: {}", profile_name);
        throw std::runtime_error("Failed to get profile by name: " + profile_name);
    }
    return expected_profile.value();
}

void IspBlender::set_media_library(MediaLibrary &mediaLib)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    m_media_library = &mediaLib;
    pipeline_active = m_media_library.has_value();
}

void IspBlender::unset_media_library()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    m_media_library.reset();
    pipeline_active = false;
}

bool IspBlender::is_pipeline_active() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return pipeline_active;
}
