#include "media_library/media_library.hpp"
#include "media_library/utils.hpp"
#include "media_library/logger_macros.hpp"
#include "media_library/media_library_logger.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <iterator>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>

#define MODULE_NAME LoggerType::Api

MediaLibrary::MediaLibrary() : m_media_lib_config_manager()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary instance created");
}

media_library_return MediaLibrary::create_frontend(std::string frontend_config_string)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Creating frontend with config string");
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
    if (!frontend_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_expected.error();
    }
    m_frontend = frontend_expected.value();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend created successfully");

    media_library_return config_return = m_frontend->set_config(frontend_config_string);
    if (config_return != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return config_return;
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Frontend configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::create_frontend(frontend_config_t frontend_config)
{
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
    if (!frontend_expected.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return frontend_expected.error();
    }
    m_frontend = frontend_expected.value();
    media_library_return config_return = m_frontend->set_config(frontend_config);
    if (config_return != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return config_return;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::create_encoders(std::map<output_stream_id_t, encoder_config_t> encoder_configs)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Creating encoders");
    for (const auto &entry : encoder_configs)
    {
        std::string stream_id = entry.first;

        LOGGER__MODULE__DEBUG(MODULE_NAME, "Creating encoder for stream {}", stream_id);
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected =
            MediaLibraryEncoder::create(stream_id);
        if (!encoder_expected.has_value())
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoder for stream {}", stream_id);
            return encoder_expected.error();
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder created for stream {}", stream_id);

        m_encoders[stream_id] = encoder_expected.value();
        std::string encoder_config_string = std::visit(
            [](auto &&config) -> std::string { return read_string_from_file(config.config_path); }, entry.second);
        media_library_return config_return = m_encoders[stream_id]->set_config(encoder_config_string);
        if (config_return != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder for stream {}", stream_id);
            return config_return;
        }
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder configured for stream {}", stream_id);
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "All encoders created and configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}
media_library_return MediaLibrary::initialize(std::string medialib_config_string)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Initializing MediaLibrary with config string");
    m_media_lib_config_manager.configure_medialib(medialib_config_string);

    if (create_frontend(m_media_lib_config_manager.get_frontend_config_as_string()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return MEDIA_LIBRARY_ERROR;
    }

    if (create_encoders(m_media_lib_config_manager.get_encoder_configs()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoders");
        return MEDIA_LIBRARY_ERROR;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "MediaLibrary initialized successfully");
    set_override_parameters(m_media_lib_config_manager.m_current_profile);
    configure_isp(m_media_lib_config_manager.get_3a_config(), m_media_lib_config_manager.get_sensor_entry());

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::initialize(std::string frontend_config_json_string,
                                              std::map<output_stream_id_t, encoder_config_t> encoders_config_paths)
{
    if (create_frontend(frontend_config_json_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create frontend");
        return MEDIA_LIBRARY_ERROR;
    }

    if (create_encoders(encoders_config_paths) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create encoders");
        return MEDIA_LIBRARY_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_frontend_encoder(
    frontend_config_t frontend_config, std::map<output_stream_id_t, encoder_config_t> encoders_config)
{
    media_library_return frontend_config_return = m_frontend->set_config(frontend_config);
    if (frontend_config_return != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure frontend");
        return frontend_config_return;
    }
    for (const auto &entry : m_encoders)
    {
        media_library_return encoder_config_return = entry.second->set_config(encoders_config[entry.first]);
        if (encoder_config_return != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder for stream {}", entry.first);
            return encoder_config_return;
        }
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "All encoders configured successfully");

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::configure_isp(const std::string &_3aconfig, const std::string &sensor_entry)
{
    LOGGER__MODULE__TRACE(MODULE_NAME, "Configuring ISP with 3A config: {} and sensor entry: {}", _3aconfig,
                          sensor_entry);

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t_now), "%Y%m%d%H%M%S");

    // Construct destination file paths in /tmp/
    std::string new_3aconfig = "/tmp/" + fs::path(_3aconfig).filename().string() + "_" + timestamp.str();
    std::string new_sensor_entry = "/tmp/" + fs::path(sensor_entry).filename().string() + "_" + timestamp.str();

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Copying 3A config to {}", new_3aconfig);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Copying sensor entry to {}", new_sensor_entry);

    // Copy files to /tmp/
    if (!fs::copy_file(_3aconfig, new_3aconfig, fs::copy_options::overwrite_existing))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to copy 3A config to {}", new_3aconfig);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    if (!fs::copy_file(sensor_entry, new_sensor_entry, fs::copy_options::overwrite_existing))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to copy sensor entry to {}", new_sensor_entry);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Create or update symlinks
    std::string symlink_3aconfig = "/usr/bin/isp_3aconfig_0";
    std::string symlink_sensor = "/usr/bin/isp_sensor_0_entry";

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Removing old symlinks");
    try
    {
        safe_remove_symlink_target(symlink_3aconfig);
        safe_remove_symlink_target(symlink_sensor);
    }
    catch (fs::filesystem_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to remove old symlinks: {}", e.what());
    }

    try
    {
        fs::create_symlink(new_3aconfig, symlink_3aconfig);
        fs::create_symlink(new_sensor_entry, symlink_sensor);
    }
    catch (fs::filesystem_error &e)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create symlinks: {}", e.what());
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "ISP configured successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::set_override_parameters(ProfileConfig profile)
{
    ProfileConfig previous_profile = m_media_lib_config_manager.m_current_profile;
    // Verify that denoise / hdr / didn't change
    // Schema profile codec_configs entire encoder_config_t
    m_media_lib_config_manager.set_profile(profile);
    bool restart_required = stream_restart_required(previous_profile);
    if (restart_required)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Restarting pipeline");
        stop_pipeline();
        configure_isp(m_media_lib_config_manager.get_3a_config(), m_media_lib_config_manager.get_sensor_entry());
    }
    configure_frontend_encoder(m_media_lib_config_manager.get_frontend_config(),
                               m_media_lib_config_manager.get_encoder_configs());
    if (restart_required)
    {
        sleep(0.5);
        start_pipeline();
    }
    return MEDIA_LIBRARY_SUCCESS;
}

bool MediaLibrary::stream_restart_required(ProfileConfig previous_profile)
{
    ProfileConfig new_profile = m_media_lib_config_manager.m_current_profile;
    // ISP changes
    bool restart_required =
        previous_profile.isp_config_files.sensor_entry_path != new_profile.isp_config_files.sensor_entry_path;

    // Res changes
    for (const auto &resolution : previous_profile.multi_resize_config.application_input_streams_config.resolutions)
    {
        if (std::find(new_profile.multi_resize_config.application_input_streams_config.resolutions.begin(),
                      new_profile.multi_resize_config.application_input_streams_config.resolutions.end(),
                      resolution) == new_profile.multi_resize_config.application_input_streams_config.resolutions.end())
        {
            restart_required |= true;
            break;
        }
    }
    // if rotation is 90 or 180 restart is required
    restart_required |= previous_profile.multi_resize_config.rotation_config.effective_value() !=
                        new_profile.multi_resize_config.rotation_config.effective_value();
    return restart_required;
}

media_library_return MediaLibrary::set_profile(std::string profile_name)
{
    // verify that profile_name exists in medialib_config
    if (m_media_lib_config_manager.m_medialib_config.profiles.find(profile_name) ==
        m_media_lib_config_manager.m_medialib_config.profiles.end())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Profile name '{}' does not exist in medialib_config", profile_name);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return set_override_parameters(m_media_lib_config_manager.m_medialib_config.profiles[profile_name]);
}

tl::expected<ProfileConfig, media_library_return> MediaLibrary::get_current_profile() const
{
    // TODO: sync encoder config sync frontend config
    return m_media_lib_config_manager.m_current_profile;
}

media_library_return MediaLibrary::subscribe_to_frontend_output(FrontendCallbacksMap fe_callbacks)
{
    m_frontend->subscribe(fe_callbacks);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::subscribe_to_encoder_output(output_stream_id_t streamId, AppWrapperCallback callback)
{
    m_encoders[streamId]->subscribe(callback);
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::start_pipeline()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Starting pipeline");
    for (const auto &entry : m_encoders)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Starting encoder for stream {}", entry.first);
        entry.second->start();
    }
    m_frontend->start();
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline started successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return MediaLibrary::stop_pipeline()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Stopping pipeline");
    m_frontend->stop();
    for (const auto &entry : m_encoders)
    {
        LOGGER__MODULE__TRACE(MODULE_NAME, "Stopping encoder for stream {}", entry.first);
        entry.second->stop();
    }
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Pipeline stopped successfully");
    return MEDIA_LIBRARY_SUCCESS;
}

MediaLibrary::~MediaLibrary()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Destroying MediaLibrary instance");
    m_frontend = nullptr;
    m_encoders.clear();
    std::string symlink_3aconfig = "/usr/bin/isp_3aconfig_0";
    std::string symlink_sensor = "/usr/bin/isp_sensor_0_entry";
    safe_remove_symlink_target(symlink_3aconfig);
    safe_remove_symlink_target(symlink_sensor);
}

MediaLibrary::MediaLibrary(MediaLibraryFrontendPtr frontend,
                           std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders)
{
    m_frontend = frontend;
    m_encoders = encoders;
}
