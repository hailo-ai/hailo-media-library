#include "config_attacher.hpp"

#include <memory>
#include <optional>
#include <string>

#include "media_library_logger.hpp"
#include "media_library_types.hpp"

#define MODULE_NAME LoggerType::ConfigAttacher

ConfigAttacher::ConfigAttacher(const ConfigManagerInteractor *interactor) : m_interactor(interactor)
{
}

ConfigAttacher::~ConfigAttacher() = default;

bool ConfigAttacher::attach_config(HailoMediaLibraryBufferPtr buffer)
{
    if (!m_interactor)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ConfigManagerInteractor is null, cannot attach config to buffer");
        return false;
    }
    if (!buffer)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Buffer is null, cannot attach config to buffer");
        return false;
    }
    auto current_profile_opt = m_interactor->get_current_profile();
    if (!current_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No profile is currently in use, cannot attach config to buffer");
        return false;
    }
    auto current_profile = current_profile_opt.value();

    LOGGER__MODULE__TRACE(MODULE_NAME, "Attaching current profile to buffer: {}", current_profile->name);
    buffer->attach_profile(current_profile);
    return true;
}

bool ConfigAttacher::attach_fallback_config(HailoMediaLibraryBufferPtr buffer)
{
    if (!m_interactor)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "ConfigManagerInteractor is null, cannot attach config to buffer");
        return false;
    }
    if (!buffer)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Buffer is null, cannot attach config to buffer");
        return false;
    }
    auto fallback_profile_opt = m_interactor->get_fallback_profile();
    if (!fallback_profile_opt.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "No profile is currently in use, cannot attach config to buffer");
        return false;
    }
    auto fallback_profile = fallback_profile_opt.value();

    LOGGER__MODULE__TRACE(MODULE_NAME, "Attaching fallback profile to buffer: {}", fallback_profile->name);
    buffer->attach_profile(fallback_profile);
    return true;
}
