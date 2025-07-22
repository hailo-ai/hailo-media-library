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
#include "media_library/throttling_state_monitor.hpp"
#include "media_library/analytics_db.hpp"

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
     *
     * @details
     * - An update to one of the following fields will trigger an internal stream reset:
     *   - **"input_video"**
     *   - **"application_input_streams"**
     *   - **"rotation"**
     *   - **"isp" section**
     *
     * - Changing the HDR or AI-denoise state from enabled to disabled or vice versa is **not allowed** by this API.
     *   The function will return an error return code if such a change is attempted.
     */
    media_library_return set_override_parameters(ProfileConfig profile);

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
     * @brief Retrieves the profile with the given name.
     *
     * @param profile_name The name of the profile to be retrieved.
     * @return tl::expected<ProfileConfig, media_library_return> The profile with the given name.
     */
    tl::expected<ProfileConfig, media_library_return> get_profile(const std::string &profile_name);

    /**
     * @brief Retrieves the current profile.
     *
     * This function returns the current profile being used.
     *
     * @return tl::expected<ProfileConfig, media_library_return> An expected object containing the current profile
     * configuration if successful, or an error code otherwise.
     */
    tl::expected<ProfileConfig, media_library_return> get_current_profile();

    /**
     * @brief Checks if a stream restart is required based on the provided profile.
     * @param previous_profile The configuration of the previously active profile, used to determine if a stream restart
     * is necessary.
     * @return A boolean value indicating whether a stream restart is required.
     */
    bool stream_restart_required(ProfileConfig previous_profile);

    MediaLibraryFrontendPtr m_frontend;                              ///< Pointer to the frontend object.
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> m_encoders; ///< Map of output stream IDs to encoder pointers.
    MediaLibConfigManager m_media_lib_config_manager; ///< Manager for media library configuration settings.

    /**
     *  @brief Set the On profile restricted user callback.
     *
     * When the system is under some event that restricts the current profile.
     * Media Library will restrict the current profile automatically and set the default profile.
     * In some scenarios, the default profile may include some specific restriction (AI denoise for example), Media
     * Library will also configure it off.
     *
     * After automatically switching, The user callback will be called with both profiles (previous and new).
     *
     * @param callback The callback to be set - includes the previous and new restricted profiles.
     * @return media_library_return The result of the operation.
     */
    media_library_return on_profile_restricted(std::function<void(ProfileConfig, ProfileConfig)> callback);

    /**
     * * @brief Set the On profile restriction done user callback.
     *
     * When the system has recovered from the event that restricted the current profile,
     * Media Library will only notify the user that the profile restriction is ended, without changing the profile back
     *
     * @param callback The callback to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return on_profile_restriction_done(std::function<void()> callback);

    /**
     * @brief Set the On pipeline state change user callback.
     *
     * When the pipeline state changes, this callback will be called.
     * @param callback The callback to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return on_pipeline_state_change(std::function<void(media_library_pipeline_state_t)> callback);

    /**
     * @brief Get the current pipeline state.
     * @return The current pipeline state (STARTED or STOPPED).
     */
    media_library_pipeline_state_t get_pipeline_state() const;

    /**
     * @brief Get the analytics database.
     * @return Reference to the analytics database.
     */
    AnalyticsDB &get_analytics_db();

  private:
    std::mutex m_mutex;
    bool m_enable_profile_restriction;               ///< Flag to enable profile restriction.
    media_library_pipeline_state_t m_pipeline_state; ///< State of the media pipeline (STARTED or STOPPED).
    std::shared_ptr<ThrottlingStateMonitor> m_throttling_monitor;
    std::function<void(media_library_pipeline_state_t)> m_pipeline_state_change_callback;
    std::function<void(ProfileConfig, ProfileConfig)> m_profile_restricted_callback;
    std::function<void()> m_profile_restriction_done_callback;

    media_library_return stop_pipeline_internal();
    media_library_return start_pipeline_internal();
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

    /**
     * @brief Validate the profile restrictions of thermal state.
     * @param profile The profile to be validated.
     * @return Whether the profile is valid.
     */
    bool validate_profile_restrictions(const ProfileConfig &profile);

    /**
     * @brief Initialize the thermal throttling monitor.
     * @return Status of the initialization.
     */
    media_library_return initialize_thermal_throttling_monitor();

    /**
     * @brief React to thermal state change
     * @return Status of the initialization.
     */
    media_library_return on_throttling_state_change(throttling_state_t state);

    /**
     * @brief React to pipeline state change  - disable denoise.
     * @return Status of the initialization.
     */
    media_library_return restrict_profile_denoise_off();

    /**
     * @brief Update the frontend configuration in profile.
     * @return Status of the update.
     */
    media_library_return update_frontend_config();
};
