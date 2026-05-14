/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/**
 * @file media_library.hpp
 * @brief MediaLibrary main API module for managing frontend and encoder operations
 **/
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include "encoder.hpp"
#include "frontend.hpp"
#include "config_manager.hpp"
#include "throttling_state_monitor.hpp"
#include "analytics_db.hpp"
#include "media_library_types.hpp"
#include "media_library_api_types.hpp"

namespace v4l2
{
class v4l2ControlManager;
}

namespace fs = std::filesystem;

class MediaLibraryInterface;
using MediaLibraryInterfacePtr = std::shared_ptr<MediaLibraryInterface>;

/**
 * @class MediaLibraryInterface
 * @brief Pure virtual interface for the MediaLibrary API.
 *
 * Both MediaLibrary (local pipeline) and MediaLibraryClient (gRPC client)
 * inherit from this interface, allowing polymorphic usage.
 */
class MediaLibraryInterface
{
  public:
    virtual ~MediaLibraryInterface() = default;

    // Initialization
    virtual media_library_return initialize(std::string medialib_config_string, bool should_restore_backup = false) = 0;

    // Pipeline control
    virtual media_library_return start_pipeline() = 0;
    virtual media_library_return stop_pipeline() = 0;
    virtual media_library_return shutdown() = 0;

    // Profile management
    virtual media_library_return set_profile(const std::string &profile_name) = 0;
    virtual media_library_return set_override_parameters(const config_profile_t &profile) = 0;
    virtual media_library_return set_automatic_algorithm_configuration(std::string automatic_algorithms) = 0;
    virtual void set_override_persistent_settings(bool override_persistent_settings) = 0;
    virtual media_library_return reset_profiles() = 0;
    virtual media_library_return set_auto_profile_restriction_enabled(bool enabled) = 0;
    virtual media_library_return set_restriction_fallback_profile(const std::string &profile_name) = 0;
    virtual bool get_auto_profile_restriction_enabled() = 0;
    virtual tl::expected<config_profile_t, media_library_return> get_profile(const std::string &profile_name) = 0;
    virtual tl::expected<config_profile_t, media_library_return> get_current_profile() = 0;
    virtual tl::expected<std::string, media_library_return> get_current_profile_str() = 0;
    virtual bool stream_restart_required(const config_profile_t &previous_profile,
                                         const config_profile_t &new_profile) = 0;

    // Frontend / encoder access
    virtual tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_frontend_output_streams() = 0;
    virtual media_library_return unsubscribe_all_from_frontend() = 0;
    virtual media_library_return add_buffer_to_encoder(output_stream_id_t stream_id,
                                                       HailoMediaLibraryBufferPtr buffer) = 0;
    virtual media_library_return add_buffer_to_frontend(HailoMediaLibraryBufferPtr buffer) = 0;

    // Subscription / callbacks
    virtual media_library_return subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks) = 0;
    virtual media_library_return subscribe_to_encoder_output(output_stream_id_t streamId,
                                                             AppWrapperCallback callback) = 0;
    virtual media_library_return unsubscribe_from_encoder_output(output_stream_id_t streamId) = 0;
    virtual media_library_return on_profile_restricted(
        std::function<void(config_profile_t, config_profile_t)> callback) = 0;
    virtual media_library_return subscribe_to_profile_restricted(
        std::function<void(const config_profile_t &, const config_profile_t &)> callback) = 0;
    virtual media_library_return on_profile_restriction_done(std::function<void()> callback) = 0;
    virtual media_library_return subscribe_to_profile_restriction_done(std::function<void()> callback) = 0;
    virtual media_library_return on_pipeline_state_change(
        std::function<void(media_library_pipeline_state_t)> callback) = 0;
    virtual media_library_return subscribe_to_pipeline_state_change(
        std::function<void(media_library_pipeline_state_t)> callback) = 0;
    virtual media_library_return subscribe_to_throttling_state_change(
        std::function<void(media_library_throttling_state_t)> callback) = 0;
    virtual media_library_return unsubscribe_from_profile_restriction_callbacks() = 0;
    virtual media_library_return unsubscribe_from_throttling_state_change() = 0;

    // State / monitoring
    virtual media_library_pipeline_state_t get_pipeline_state() const = 0;
    virtual tl::expected<media_library_throttling_state_t, media_library_return> get_throttling_state() const = 0;
    virtual AnalyticsDB &get_analytics_db() = 0;

    // Backup
    virtual media_library_return backup_profiles() = 0;
    virtual void set_default_backup_folder_path(const std::string &path) = 0;
};

class MediaLibrary;
using MediaLibraryPtr = std::shared_ptr<MediaLibrary>;

/**
 * @class MediaLibrary
 * @brief A class to manage media library operations including frontend and encoders.
 */
class MediaLibrary : public MediaLibraryInterface
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
     * @brief Initialize the media library with a configuration string.
     * @param medialib_config_string string containing the media library configuration.
     * @param should_restore_backup Whether to restore the backup configuration, if exists.
     * @return Status of the initialization.
     */
    media_library_return initialize(std::string medialib_config_string, bool should_restore_backup = false) override;

    /**
     * @brief Subscribe to frontend output.
     * @param fe_callbacks Map of frontend callbacks.
     * @return Status of the subscription.
     */
    media_library_return subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks) override;

    /**
     * @brief Subscribe to frontend output receiving the original GstBuffer.
     *
     * Unlike subscribe_to_frontend_output(), the callback receives the
     * original GstBuffer produced by the internal frontend pipeline,
     * avoiding a teardown/rebuild cycle. The GstBuffer is reffed before
     * the callback; the callee owns that ref.
     *
     * @param fe_callbacks Map of GstBuffer callbacks keyed by stream id.
     * @return Status of the subscription.
     */
    media_library_return subscribe_to_frontend_gst_output(FrontendGstBufferCallbacksMap fe_callbacks);

    /**
     * @brief Subscribe to encoder output.
     * @param streamId ID of the output stream.
     * @param callback Application wrapper callback.
     * @return Status of the subscription.
     */
    media_library_return subscribe_to_encoder_output(output_stream_id_t streamId, AppWrapperCallback callback) override;
    media_library_return unsubscribe_from_encoder_output(output_stream_id_t streamId) override;

    /**
     * @brief Start the media pipeline.
     * @return Status of the operation.
     */
    media_library_return start_pipeline() override;

    /**
     * @brief Stop the media pipeline.
     * @return Status of the operation.
     */
    media_library_return stop_pipeline() override;

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
    media_library_return set_override_parameters(const config_profile_t &profile) override;

    /**
     * @brief Set the automatic algorithm configuration as json object
     *
     * @param automatic_algorithms
     * @return medialibrary_return
     */
    media_library_return set_automatic_algorithm_configuration(std::string automatic_algorithms) override;

    /**
     * @brief Sets the profile for the media library.
     *
     * This function sets the profile for the media library using the provided profile name.
     * If override persistent settings is enabled, the persistent settings will be overridden by the previous profile.
     *
     * @param profile_name The name of the profile to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return set_profile(const std::string &profile_name) override;

    /**
     * @brief Sets the override persistent settings flag.
     *
     * This function sets the override persistent settings flag.
     *
     * @param override_persistent_settings Whether to override the persistent settings.
     */
    void set_override_persistent_settings(bool override_persistent_settings) override;

    /**
     * @brief Reset any overrides made to the profiles.
     *
     * This function resets any overrides made to the profiles, restoring them to their default state.
     *
     * @return media_library_return The result of the operation.
     */
    media_library_return reset_profiles() override;

    /**
     * @brief Enable or disable automatic profile restriction switching feature.
     * This feature allows the media library to automatically switch profiles and notify the user when a restriction
     * occurs due to throttling conditions. Automatically switch to the fallback profile upon restriction. Enabled by
     * default.
     * @param enabled True to enable automatic profile restriction switching, false to disable.
     * @return media_library_return The result of the operation.
     */
    media_library_return set_auto_profile_restriction_enabled(bool enabled) override;

    /**
     * @brief Sets the restriction fallback profile.
     *
     * This function sets the fallback profile to be used when the system is under throttling conditions and the current
     * profile is restricted. Without setting this, the default profile will be used as the fallback profile.
     *
     * @param profile_name The name of the fallback profile to be used on restriction.
     * @return media_library_return The result of the operation.
     */
    media_library_return set_restriction_fallback_profile(const std::string &profile_name) override;

    /**
     * @brief Get the automatic profile restriction switching feature enabled state.
     * @return boolean
     */
    bool get_auto_profile_restriction_enabled() override;

    /**
     * @brief Retrieves the profile with the given name.
     *
     * @param profile_name The name of the profile to be retrieved.
     * @return tl::expected<config_profile_t, media_library_return> The profile with the given name.
     */
    tl::expected<config_profile_t, media_library_return> get_profile(const std::string &profile_name) override;

    /**
     * @brief Retrieves the current profile.
     *
     * This function returns the current profile being used.
     *
     * @return tl::expected<config_profile_t, media_library_return> An expected object containing the
     * current profile configuration if successful, or an error code otherwise.
     */
    tl::expected<config_profile_t, media_library_return> get_current_profile() override;

    /**
     * @brief Retrieves the current profile as a JSON string.
     *
     * This function returns the current profile being used in JSON string format.
     *
     * @return tl::expected<std::string, media_library_return> An expected object containing the
     * current profile as a JSON string if successful, or an error code otherwise.
     */
    tl::expected<std::string, media_library_return> get_current_profile_str() override;

    /**
     * @brief Checks if a stream restart is required based on the provided profile.
     * @param previous_profile The configuration of the previously active profile, used to determine if a stream restart
     * is necessary.
     * @return A boolean value indicating whether a stream restart is required.
     */
    bool stream_restart_required(const config_profile_t &previous_profile,
                                 const config_profile_t &new_profile) override;

    /// @deprecated Direct access to m_frontend is deprecated.
    MediaLibraryFrontendPtr m_frontend;
    /// @deprecated Direct access to m_encoders is deprecated.
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> m_encoders;

    /**
     * @brief Get the output streams from the frontend.
     * @return tl::expected containing a vector of frontend_output_stream_t or an error code.
     */
    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_frontend_output_streams() override;

    /**
     * @brief Unsubscribe all callbacks from the frontend output streams.
     * @return Status of the operation.
     */
    media_library_return unsubscribe_all_from_frontend() override;

    /**
     * @brief Add a buffer to a specific encoder for encoding.
     * @param stream_id ID of the output stream / encoder.
     * @param buffer Shared pointer to the buffer to be encoded.
     * @return Status of the operation.
     */
    media_library_return add_buffer_to_encoder(output_stream_id_t stream_id,
                                               HailoMediaLibraryBufferPtr buffer) override;

    /**
     * @brief Add a buffer to the frontend input for processing.
     *
     * Used for APPSRC mode where frames are pushed into the frontend
     * from an external source (e.g., file reader).
     *
     * @param buffer Shared pointer to the buffer to be processed.
     * @return Status of the operation.
     */
    media_library_return add_buffer_to_frontend(HailoMediaLibraryBufferPtr buffer) override;

    /**
     *  @brief Set the On profile restricted user callback.
     *
     * When the system is under thermal event that restricts the current profile.
     * Media Library will restrict the current profile automatically and set the default profile as a fallback.
     * In some scenarios, the default profile may include some specific restriction (AI-ISP for example), Media
     * Library will also configure it off.
     *
     * You can override the restricted fallback profile using set_restriction_fallback_profile function.
     *
     * After automatically switching, the user callback will be called with both profiles (previous and new).
     *
     * @param callback The callback to be set - includes the previous and new restricted profiles.
     * @return media_library_return The result of the operation.
     */
    media_library_return on_profile_restricted(
        std::function<void(config_profile_t, config_profile_t)> callback) override;

    /**
     *  @brief Subscribe to profile restricted user callback.
     *
     * When the system is under thermal event that restricts the current profile.
     * Media Library will restrict the current profile automatically and set the default profile as a fallback.
     * In some scenarios, the default profile may include some specific restriction (AI-ISP for example), Media
     * Library will also configure it off.
     *
     * You can override the restricted fallback profile using set_restriction_fallback_profile function.
     *
     * After automatically switching, the user callback will be called with both profiles (previous and new).
     *
     * @note Only one subscriber is supported. Calling subscribe_to_profile_restricted
     * again will replace the existing callback. Use unsubscribe_from_profile_restriction_callbacks
     * before re-subscribing if you need to ensure no callback races.
     *
     * @param callback The callback to be set - includes the previous and new restricted profiles.
     * @return media_library_return The result of the operation.
     */
    media_library_return subscribe_to_profile_restricted(
        std::function<void(const config_profile_t &, const config_profile_t &)> callback) override;

    /**
     * * @brief Subscribe to the profile restriction done user callback.
     *
     * When the system has recovered from the event that restricted the current profile,
     * Media Library will only notify the user that the profile restriction is ended, without changing the profile back
     *
     * @param callback The callback to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return on_profile_restriction_done(std::function<void()> callback) override;

    /**
     * * @brief Subscribe to the profile restriction done user callback.
     *
     * When the system has recovered from the event that restricted the current profile,
     * Media Library will only notify the user that the profile restriction is ended, without changing the profile back
     *
     * @param callback The callback to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return subscribe_to_profile_restriction_done(std::function<void()> callback) override;

    /**
     * @brief Subscribe to pipeline state change user callback.
     *
     * When the pipeline state changes, this callback will be called.
     * @param callback The callback to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return on_pipeline_state_change(
        std::function<void(media_library_pipeline_state_t)> callback) override;

    /**
     * @brief Subscribe to pipeline state change user callback.
     *
     * When the pipeline state changes, this callback will be called.
     *
     * @note Only one subscriber is supported. Calling subscribe_to_pipeline_state_change
     * again will replace the existing callback. Use unsubscribe before re-subscribing
     * if you need to ensure no callback races.
     *
     * @param callback The callback to be set.
     * @return media_library_return The result of the operation.
     */
    media_library_return subscribe_to_pipeline_state_change(
        std::function<void(media_library_pipeline_state_t)> callback) override;

    /**
     * @brief Get the current pipeline throttling state.
     *
     * @return tl::expected<media_library_throttling_state_t, media_library_return>
     * The current pipeline throttling state or an error code.
     */
    tl::expected<media_library_throttling_state_t, media_library_return> get_throttling_state() const override;

    /**
     * @brief Subscribe to throttling state change events.
     *
     * @note Only one subscriber is supported. Calling subscribe_to_throttling_state_change
     * again will replace the existing callback. Use unsubscribe_from_throttling_state_change
     * before re-subscribing if you need to ensure no callback races.
     *
     * @param callback The callback function to be called on throttling state changes.
     * @return media_library_return The result of the operation.
     */
    media_library_return subscribe_to_throttling_state_change(
        std::function<void(media_library_throttling_state_t)> callback) override;

    /**
     * @brief Unsubscribe from all profile restriction callbacks.
     *
     * Removes the callbacks registered via subscribe_to_profile_restricted and
     * subscribe_to_profile_restriction_done. This should be called during cleanup
     * to prevent callbacks being invoked on destroyed objects.
     *
     * @return media_library_return The result of the operation.
     */
    media_library_return unsubscribe_from_profile_restriction_callbacks() override;

    /**
     * @brief Unsubscribe from throttling state change events.
     *
     * Removes the callback registered via subscribe_to_throttling_state_change.
     * This should be called during cleanup to prevent callbacks being invoked on destroyed objects.
     *
     * @return media_library_return The result of the operation.
     */
    media_library_return unsubscribe_from_throttling_state_change() override;

    /**
     * @brief Get the current pipeline state.
     * @return The current pipeline state (STARTED or STOPPED).
     */
    media_library_pipeline_state_t get_pipeline_state() const override;

    /**
     * @brief Get the analytics database.
     * @return Reference to the analytics database.
     */
    AnalyticsDB &get_analytics_db() override;

    /**
     * @brief Gracefully shuts down the media library instance.
     *
     * Stops all running pipelines, releases allocated resources, and performs
     * any necessary cleanup before exiting. After calling this function, the
     * instance should not be used again unless it is re-initialized.
     *
     * @return MEDIA_LIBRARY_SUCCESS on success, or an appropriate error code
     *         of type media_library_return on failure.
     */
    media_library_return shutdown() override;

    /**
     * @brief Backup the current profiles state to the configured backup folder
     *
     * Creates a folder with JSON files for each profile containing the current profile state, including
     * the active profile name and all profile configurations.
     *
     * @return media_library_return The result of the operation.
     */
    media_library_return backup_profiles() override;

    /**
     * @brief Set the default backup folder path
     *
     * @param path Path to the default backup folder
     */
    void set_default_backup_folder_path(const std::string &path) override;

  private:
    media_library_return initialize_internal(std::string medialib_config_string);
    std::recursive_mutex m_mutex;
    std::recursive_mutex
        m_profile_change_mutex;        ///< Mutex for profile change operations (recursive to allow callback re-entry).
    bool m_enable_profile_restriction; ///< Flag to enable profile restriction.
    bool m_override_persistent_settings;                         ///< Flag to override persistent settings.
    std::string m_default_backup_folder_path;                    ///< Default backup folder path.
    media_library_throttling_state_t m_current_throttling_state; ///< Current throttling state.
    media_library_pipeline_state_t m_pipeline_state;             ///< State of the media pipeline (STARTED or STOPPED).
    std::shared_ptr<ThrottlingStateMonitor> m_throttling_monitor;
    throttling_monitor_user_id_t m_throttling_monitor_user_id{
        INVALID_THROTTLING_MONITOR_USER_ID}; ///< User ID for throttling monitor
    std::function<void(media_library_pipeline_state_t)> m_pipeline_state_change_callback = nullptr;
    std::function<void(const config_profile_t &, const config_profile_t &)> m_profile_restricted_callback = nullptr;
    std::function<void()> m_profile_restriction_done_callback = nullptr;
    std::function<void(media_library_throttling_state_t)> m_throttling_state_change_callback = nullptr;
    std::optional<std::string> m_restriction_fallback_profile; ///< Fallback profile name for restriction scenarios.
    std::unique_ptr<ConfigManagerInteractor>
        m_config_manager_interactor; ///< Manager for media library configuration settings.

    media_library_return stop_pipeline_internal();
    media_library_return start_pipeline_internal();
    /**
     * @brief Create the frontend with the given configuration.
     * @param frontend_config Configuration for the frontend.
     * @return Status of the creation.
     */
    media_library_return create_frontend(std::string frontend_config_json_string);

    /**
     * @brief Create an encoder with the given configuration.
     * @param stream_id ID of the output stream.
     * @param encoded_output_stream Configuration for the encoder.
     * @return Status of the creation.
     */
    media_library_return create_encoder(output_stream_id_t stream_id,
                                        config_encoded_output_stream_t encoded_output_stream);
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
     * @brief Validate the profile restrictions of thermal state.
     * @param profile The profile to be validated.
     * @return Whether the profile is valid.
     */
    bool validate_profile_thermal_restrictions(const config_profile_t &profile);

    /**
     * @brief Convert throttling state to media library throttling state.
     * @param state Throttling state.
     * @return Media library throttling state.
     */
    media_library_throttling_state_t convert_to_media_library_throttling_state(throttling_state_t state);

    /**
     * @brief Validate the profile against restriction rules.
     * @return Status of the validation.
     */
    media_library_return validate_profile_rules();

    /**
     * @brief Get the encoder type from the encoder configuration variant.
     * @param config_variant The encoder configuration variant.
     * @return The encoder type.
     */
    EncoderType get_encoder_type(const encoder_config_t &config_variant);

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

    media_library_return restrict_denoise_off_switch_to_fallback_profile(const config_profile_t &current_profile);
    /**
     * @brief Automatically switch to the fallback profile upon restriction.
     *
     * @param fallback_profile The profile to switch to when restriction occurs.
     * @return media_library_return The result of the operation.
     */
    media_library_return restriction_auto_switch_to_fallback_profile(config_profile_t &fallback_profile);

    /**
     * @brief Configure privacy mask on the given encoder.
     *
     * @param encoder
     * @param privacy_mask_config
     * @return media_library_return
     */
    media_library_return configure_privacy_mask(MediaLibraryEncoderPtr encoder,
                                                const privacy_mask_config_t &privacy_mask_config);

    media_library_return handle_restricted_streaming_state();

    /**
     * @brief Configure OSD on the given encoder.
     * @param encoder
     * @param osd_config
     * @return media_library_return
     */
    media_library_return configure_osd(MediaLibraryEncoderPtr encoder, const config_stream_osd_t &osd_config);
    /**
     * @brief Checks if frontend pause/unpause is required based on the provided profile.
     * @param previous_profile The configuration of the previously active profile.
     * @param new_profile The configuration of the new profile.
     * @param restart_required Indicates whether a stream restart is required. if restart is required, pause is not
     * relevant.
     * @return A boolean value indicating whether frontend pause/unpause is required.
     */
    bool frontend_pause_required(config_profile_t previous_profile, config_profile_t new_profile,
                                 bool restart_required);

    /**
     * @brief override the existing 3A config file, with the current 3A config struct.
     * @return Status of the update.
     */
    media_library_return update_3a_config_file();
};

namespace media_library_service
{

/** @brief Default address for the Media Library gRPC service (localhost — same device) */
constexpr const char *DEFAULT_SERVICE_ADDRESS = "localhost";
/** @brief Default port for the Media Library gRPC service */
constexpr uint DEFAULT_SERVICE_PORT = 50051;

/**
 * @class MediaLibraryClient
 * @brief gRPC client implementing MediaLibraryInterface for interacting with the Media Library Service.
 *
 * This client provides async communication with the MediaLibraryService
 * for initializing, starting, and stopping pipelines, as well as setting profiles.
 * Methods not yet supported over gRPC return MEDIA_LIBRARY_UNINITIALIZED.
 */
class MediaLibraryClient : public MediaLibraryInterface
{
  public:
    /**
     * @brief Constructor for MediaLibraryClient
     * @param address ip address of the Media Library Service (default: localhost)
     * @param port port number of the Media Library Service (default: 50051)
     */
    explicit MediaLibraryClient(std::string address = DEFAULT_SERVICE_ADDRESS, uint port = DEFAULT_SERVICE_PORT);

    /**
     * @brief Destructor
     */
    ~MediaLibraryClient();

    /**
     * @brief Factory method to create a MediaLibraryClient
     * @param address ip address of the Media Library Service (default: localhost)
     * @param port port number of the Media Library Service (default: 50051)
     * @return MediaLibraryInterfacePtr shared pointer to the created client
     */
    static tl::expected<MediaLibraryInterfacePtr, media_library_return> create(
        const std::string &address = DEFAULT_SERVICE_ADDRESS, uint port = DEFAULT_SERVICE_PORT);

    // --- Implemented via gRPC ---

    media_library_return initialize(std::string medialib_config_string, bool should_restore_backup = false) override;
    media_library_return start_pipeline() override;
    media_library_return stop_pipeline() override;
    media_library_return set_profile(const std::string &profile_name) override;
    media_library_pipeline_state_t get_pipeline_state() const override;

    // --- Not yet supported in remote mode (return UNINITIALIZED or defaults) ---

    media_library_return shutdown() override;
    media_library_return set_override_parameters(const config_profile_t &profile) override;
    media_library_return set_automatic_algorithm_configuration(std::string automatic_algorithms) override;
    void set_override_persistent_settings(bool override_persistent_settings) override;
    media_library_return reset_profiles() override;
    media_library_return set_auto_profile_restriction_enabled(bool enabled) override;
    media_library_return set_restriction_fallback_profile(const std::string &profile_name) override;
    bool get_auto_profile_restriction_enabled() override;
    tl::expected<config_profile_t, media_library_return> get_profile(const std::string &profile_name) override;
    tl::expected<config_profile_t, media_library_return> get_current_profile() override;
    tl::expected<std::string, media_library_return> get_current_profile_str() override;
    bool stream_restart_required(const config_profile_t &previous_profile,
                                 const config_profile_t &new_profile) override;

    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_frontend_output_streams() override;
    media_library_return unsubscribe_all_from_frontend() override;
    media_library_return add_buffer_to_encoder(output_stream_id_t stream_id,
                                               HailoMediaLibraryBufferPtr buffer) override;
    media_library_return add_buffer_to_frontend(HailoMediaLibraryBufferPtr buffer) override;
    media_library_return subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks) override;
    media_library_return subscribe_to_encoder_output(output_stream_id_t streamId, AppWrapperCallback callback) override;
    media_library_return unsubscribe_from_encoder_output(output_stream_id_t streamId) override;
    media_library_return on_profile_restricted(
        std::function<void(config_profile_t, config_profile_t)> callback) override;
    media_library_return subscribe_to_profile_restricted(
        std::function<void(const config_profile_t &, const config_profile_t &)> callback) override;
    media_library_return on_profile_restriction_done(std::function<void()> callback) override;
    media_library_return subscribe_to_profile_restriction_done(std::function<void()> callback) override;
    media_library_return on_pipeline_state_change(
        std::function<void(media_library_pipeline_state_t)> callback) override;
    media_library_return subscribe_to_pipeline_state_change(
        std::function<void(media_library_pipeline_state_t)> callback) override;
    media_library_return subscribe_to_throttling_state_change(
        std::function<void(media_library_throttling_state_t)> callback) override;
    media_library_return unsubscribe_from_profile_restriction_callbacks() override;
    media_library_return unsubscribe_from_throttling_state_change() override;

    tl::expected<media_library_throttling_state_t, media_library_return> get_throttling_state() const override;
    AnalyticsDB &get_analytics_db() override;

    media_library_return backup_profiles() override;
    void set_default_backup_folder_path(const std::string &path) override;

  private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace media_library_service
