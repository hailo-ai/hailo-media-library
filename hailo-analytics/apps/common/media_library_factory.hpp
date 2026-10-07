#pragma once

#include <cxxopts/cxxopts.hpp>
#include <media_library/media_library.hpp>
#include <media_library/media_library_types.hpp>

#include <tl/expected.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace apps::utils
{

/** @brief Name of the option that opts in to the media library service. */
inline constexpr const char *USE_MEDIALIB_SERVICE_OPTION = "use-medialib-service";

inline constexpr const char *MEDIA_LIBRARY_SERVICE_NAME = "media_library_service";
inline constexpr const char *MEDIA_LIBRARY_SERVICE_INIT_SCRIPT = "/etc/init.d/media-library-service.sh";

/**
 * @brief Message for a service that could not be reached, naming the remedy.
 */
inline std::string service_unreachable_message()
{
    return "Could not reach " + std::string(MEDIA_LIBRARY_SERVICE_NAME) + " within " +
           std::to_string(media_library_service::DEFAULT_CONNECT_TIMEOUT.count()) + "s. Start it with '" +
           std::string(MEDIA_LIBRARY_SERVICE_INIT_SCRIPT) + " start', or drop --" +
           std::string(USE_MEDIALIB_SERVICE_OPTION) + " to host the media library in this process.";
}

/**
 * @brief Message for an app whose process mode the media library service cannot serve.
 *
 * @param reason Why the service cannot serve this app, as a sentence fragment.
 */
inline std::string service_mode_unsupported_message(const std::string &reason)
{
    return "This application cannot run against " + std::string(MEDIA_LIBRARY_SERVICE_NAME) + ": " + reason +
           ". Drop --" + std::string(USE_MEDIALIB_SERVICE_OPTION) + " to host the media library in this process.";
}

/**
 * @brief Create the media library and initialize it from a configuration.
 *
 * @param use_medialib_service Connect to the service instead of hosting the library here.
 * @param medialib_config The media library configuration, as a path or as JSON content.
 * @return The initialized media library, or the error that prevented it.
 */
inline tl::expected<MediaLibraryInterfacePtr, media_library_return> create_media_library(
    bool use_medialib_service, const std::string &medialib_config)
{
    MediaLibraryInterfacePtr media_library;
    if (use_medialib_service)
    {
        auto media_lib_expected = media_library_service::MediaLibraryClient::create(
            media_library_service::DEFAULT_SERVICE_ADDRESS, media_library_service::DEFAULT_SERVICE_PORT);
        if (!media_lib_expected.has_value())
        {
            std::cerr << "Error: failed to create the media library client" << std::endl;
            return tl::make_unexpected(media_lib_expected.error());
        }
        media_library = media_lib_expected.value();
    }
    else
    {
        auto media_lib_expected = MediaLibrary::create();
        if (!media_lib_expected.has_value())
        {
            std::cerr << "Error: failed to create the media library" << std::endl;
            return tl::make_unexpected(media_lib_expected.error());
        }
        media_library = media_lib_expected.value();
    }

    media_library_return init_status = media_library->initialize(medialib_config);
    if (init_status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Error: "
                  << (use_medialib_service ? service_unreachable_message() : "failed to initialize the media library")
                  << std::endl;
        return tl::make_unexpected(init_status);
    }
    return media_library;
}

} // namespace apps::utils
