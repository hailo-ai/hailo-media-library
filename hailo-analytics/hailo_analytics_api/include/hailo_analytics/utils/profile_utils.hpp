#pragma once

#include <memory>
#include <string>

#include "media_library/media_library.hpp"

namespace hailo_analytics::utils
{

/**
 * @brief Activate the target profile from JSON without inheriting persistent fields
 *        (dewarp, OSD, etc.) from the currently-loaded default profile.
 *
 * Wraps the call in set_override_persistent_settings(false) / (true) so the target
 * profile's JSON wins on initial activation, while leaving runtime profile-switch
 * inheritance intact for subsequent set_profile() calls.
 */
inline media_library_return set_initial_profile(const MediaLibraryInterfacePtr &media_library,
                                                const std::string &profile_name)
{
    media_library->set_override_persistent_settings(false);
    auto ret = media_library->set_profile(profile_name);
    media_library->set_override_persistent_settings(true);
    return ret;
}

} // namespace hailo_analytics::utils
