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
#include "media_library/media_library_types.hpp"

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
     * @brief Constructor for the media library module
     *
     * @note This constructor is used internally by the create function.
     */
    MediaLibrary();

    /**
     * @brief Constructs a MediaLibrary object
     *
     * @return tl::expected<MediaLibraryPtr, media_library_return> -
     * An expected object that holds either a shared pointer
     *  to a MediaLibrary object, or a error code.
     */
    static tl::expected<MediaLibraryPtr, media_library_return> create();

    /**
     * @brief Initialize the media library with frontend and encoder configurations.
     * @param frontend_config Configuration for the frontend.
     * @param encoders_config Vector of encoder configurations.
     * @return Status of the initialization.
     */
    media_library_return initialize(std::string frontend_config_json_string,
                                    std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_stream);

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
    media_library_return set_override_parameters(config_profile_t profile);

    /**
     * @brief Set the automatic algorithm configuration as json object
     *
     * @param automatic_algorithms
     * @return medialibrary_return
     */
    media_library_return set_automatic_algorithm_configuration(std::string automatic_algorithms);

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
     * @return tl::expected<config_profile_t, media_library_return> The profile with the given name.
     */
    tl::expected<config_profile_t, media_library_return> get_profile(const std::string &profile_name);

    /**
     * @brief Retrieves the current profile.
     *
     * This function returns the current profile being used.
     *
     * @return tl::expected<config_profile_t, media_library_return> An expected object containing the
     * current profile configuration if successful, or an error code otherwise.
     */
    tl::expected<config_profile_t, media_library_return> get_current_profile();

    /**
     * @brief Retrieves the current profile as a JSON string.
     *
     * This function returns the current profile being used in JSON string format.
     *
     * @return tl::expected<std::string, media_library_return> An expected object containing the
     * current profile as a JSON string if successful, or an error code otherwise.
     */
    tl::expected<std::string, media_library_return> get_current_profile_str();

    /**
     * @brief Checks if a stream restart is required based on the provided profile.
     * @param previous_profile The configuration of the previously active profile, used to determine if a stream restart
     * is necessary.
     * @return A boolean value indicating whether a stream restart is required.
     */
    bool stream_restart_required(config_profile_t previous_profile, config_profile_t new_profile);

    MediaLibraryFrontendPtr m_frontend;                              ///< Pointer to the frontend object.
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> m_encoders; ///< Map of output stream IDs to encoder pointers.
    std::unique_ptr<MediaLibConfigManager>
        m_media_lib_config_manager; ///< Manager for media library configuration settings.

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
    media_library_return on_profile_restricted(std::function<void(config_profile_t, config_profile_t)> callback);

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
    std::function<void(config_profile_t, config_profile_t)> m_profile_restricted_callback;
    std::function<void()> m_profile_restriction_done_callback;
    std::optional<std::string> m_active_aaa_config_path;
    bool m_switching_full_profile;

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
    media_library_return create_encoders(
        const std::map<output_stream_id_t, config_encoded_output_stream_t> &encoded_output_stream);

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
    media_library_return configure_frontend_encoder(
        frontend_config_t frontend_config,
        std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_streams);

    /**
     * @brief Configure the isp and masking blenders with the given configurations.
     * @param encoded_output_streams Map of output stream IDs to encoder configurations.
     * @return Status of the configuration.
     */
    media_library_return configure_blenders(
        std::map<output_stream_id_t, config_encoded_output_stream_t> encoded_output_streams);

    /**
     * @brief Configure the ISP with the given configuration.
     * @param _3aconfig Path to the 3A configuration file.
     * @param sensor_entry Sensor entry for the ISP configuration.
     * @return Status of the configuration.
     */
    media_library_return configure_isp_files(const std::string &_3aconfig, const std::string &sensor_entry);

    /**
     * @brief Configure the ISP with the current profile.
     * @return Status of the configuration.
     */
    media_library_return configure_isp_with_current_profile();

    /**
     * @brief Decides from what source to configure the ISP.
     * @param restart_required Whether a restart is required.
     * @param previous_profile The previous profile configuration.
     * @param new_profile The new profile configuration.
     * @return Status of the configuration.
     */
    media_library_return configure_isp(bool restart_required, config_profile_t &previous_profile,
                                       config_profile_t &new_profile);
    /**
     * @brief override the existing 3A config file with json.
     * @param _3aconfig_json JSON string of the 3A configuration.
     * @return Status of the update.
     */
    media_library_return update_3a_config_file(const std::string &_3aconfig_json);
    /**
     * @brief Update sensor entry file with correct sensor type, sensor I2C bus, and sensor I2C address.
     * @param sensor_entry_path Path to the sensor entry file to update.
     * @return Status of the update.
     */
    media_library_return update_sensor_entry_file(const std::string &sensor_entry_path);
    /**
     * @brief Validate the profile restrictions of thermal state.
     * @param profile The profile to be validated.
     * @return Whether the profile is valid.
     */
    bool validate_profile_restrictions(const config_profile_t &profile);

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

    /**
     * @brief Configure privacy mask on the given encoder.
     *
     * @param encoder
     * @param privacy_mask_config
     * @return media_library_return
     */
    media_library_return configure_privacy_mask(MediaLibraryEncoderPtr encoder,
                                                const privacy_mask_config_t &privacy_mask_config);
};
