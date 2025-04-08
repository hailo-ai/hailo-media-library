#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/medialib_config_manager.hpp"
#include "media_library/media_library_api_types.hpp"

namespace fs = std::filesystem;

class MediaLibrary;
using MediaLibraryPtr = std::shared_ptr<MediaLibrary>;

/**
 * @class MediaLibrary
 * @brief A class to manage media library operations including frontend and encoders.
 */
class MediaLibrary
{
  public:
    /**
     * @brief Default constructor.
     */
    MediaLibrary();

    /**
     * @brief Constructor with configuration path.
     * @param medialib_config_path Path to the media library configuration file.
     */
    MediaLibrary(std::string medialib_config_path);

    /**
     * @brief Constructor with frontend and encoders.
     * @param frontend Pointer to the frontend object.
     * @param encoders Map of output stream IDs to encoder pointers.
     */
    MediaLibrary(MediaLibraryFrontendPtr frontend, std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders);

    /**
     * @brief Initialize the media library with frontend and encoder configurations.
     * @param frontend_config Configuration for the frontend.
     * @param encoders_config Vector of encoder configurations.
     * @return Status of the initialization.
     */
    media_library_return initialize(std::string frontend_config_json_string,
                                    std::map<output_stream_id_t, encoder_config_t> encoders_config);

    /**
     * @brief Initialize the media library with a configuration file.
     * @param medialib_config_path Path to the media library configuration file.
     * @return Status of the initialization.
     */
    media_library_return initialize(std::string medialib_config_path);

    /**
     * @brief Subscribe to frontend output.
     * @param fe_callbacks Map of frontend callbacks.
     * @return Status of the subscription.
     */
    media_library_return subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks);

    /**
     * @brief Subscribe to encoder output.
     * @param streamId ID of the output stream.
     * @param callback Application wrapper callback.
     * @return Status of the subscription.
     */
    media_library_return subscribe_to_encoder_output(output_stream_id_t streamId, AppWrapperCallback callback);

    /**
     * @brief Start the media pipeline.
     * @return Status of the operation.
     */
    media_library_return start_pipeline();

    /**
     * @brief Stop the media pipeline.
     * @return Status of the operation.
     */
    media_library_return stop_pipeline();

    /**
     * @brief Destructor.
     */
    ~MediaLibrary();

    /**
     * @brief Sets the profile for the media library.
     *
     * This function sets the profile for the media library using the provided profile object.
     *
     * @param profile The profile to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return set_override_profile(ProfileConfig profile);

    /**
     * @brief Sets the profile for the media library.
     *
     * This function sets the profile for the media library using the provided profile name.
     *
     * @param profile_name The name of the profile to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return set_profile(std::string profile_name);

    /**
     * @brief Retrieves the current profile.
     *
     * This function returns the current profile being used.
     *
     * @return tl::expected<ProfileConfig, media_library_return> The current profile.
     */
    tl::expected<ProfileConfig, media_library_return> get_current_profile() const;

    bool stream_restart_required(ProfileConfig previous_profile);

    MediaLibraryFrontendPtr m_frontend;                              ///< Pointer to the frontend object.
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> m_encoders; ///< Map of output stream IDs to encoder pointers.
    MediaLibConfigManager m_media_lib_config_manager;

  private:
    /**
     * @brief Create the frontend with the given configuration.
     * @param frontend_config Configuration for the frontend.
     * @return Status of the creation.
     */
    media_library_return create_frontend(std::string frontend_config_json_string);

    /**
     * @brief Create encoders with the given configurations.
     * @param encoder_config Vector of encoder configurations.
     * @return Status of the creation.
     */
    media_library_return create_encoders(std::map<output_stream_id_t, encoder_config_t> encoder_config);

    /**
     * @brief Create the frontend with the given configuration.
     * @param frontend_config Configuration for the frontend.
     * @return Status of the creation.
     */
    media_library_return create_frontend(frontend_config_t frontend_config);

    /**
     * @brief Configure frontend and encoder with the given configurations.
     * @param frontend_config Configuration for the frontend.
     * @param encoders_config Vector of encoder configurations.
     * @return Status of the configuration.
     */
    media_library_return configure_frontend_encoder(frontend_config_t frontend_config,
                                                    std::map<output_stream_id_t, encoder_config_t> encoders_config);

    /**
     * @brief Configure the ISP with the given configuration.
     * @param config Configuration for the ISP.
     * @param sensor_entry Sensor entry for the ISP.
     * @return Status of the configuration.
     */
    media_library_return configure_isp(const std::string &_3aconfig, const std::string &sensor_entry);
};
