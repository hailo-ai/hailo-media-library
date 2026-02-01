#include "media_library/examples_common.hpp"

bool set_override_parameters(config_profile_t override_profile)
{
    media_library_return profile_ret = m_media_lib->set_override_parameters(override_profile);
    if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        if (profile_ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
        {
            std::cout << "Profile is restricted at this moment, skipping" << std::endl;
        }
        else
        {
            std::cout << "Failed to override profile" << std::endl;
            return false;
        }
    }

    auto profile_exp = m_media_lib->get_current_profile();
    if (!profile_exp.has_value())
    {
        std::cout << "Failed to get current profile name" << std::endl;
        return false;
    }
    m_user_profile = profile_exp.value();
    return true;
}

int update_encoders_bitrate()
{
    uint32_t new_bitrate = 10000000;
    uint enc_i = 0;
    auto expected_profile = m_media_lib->get_current_profile();
    if (!expected_profile.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return 1;
    }
    config_profile_t profile = expected_profile.value();
    for (auto &entry : profile.to_encoder_config_map())
    {
        encoder_config_t &encoder_config = entry.second;
        if (profile.get_encoder_type(entry.first) == EncoderType::Jpeg)
        {
            continue;
        }
        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        std::cout << "Encoder " << enc_i
                  << " current bitrate: " << hailo_encoder_config.rate_control.bitrate.target_bitrate << " Setting to "
                  << new_bitrate << std::endl;
        hailo_encoder_config.rate_control.bitrate.target_bitrate = new_bitrate;
        bool profile_ret = set_override_parameters(profile);
        if (!profile_ret)
            return 1;
        enc_i++;
    }

    return 0;
}

int update_jpeg_encoders_quality()
{
    uint32_t new_quality = 75;
    uint enc_i = 0;
    auto expected_profile = m_media_lib->get_current_profile();
    if (!expected_profile.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return 1;
    }
    config_profile_t profile = expected_profile.value();
    for (auto &entry : profile.to_encoder_config_map())
    {
        encoder_config_t &encoder_config = entry.second;
        if (profile.get_encoder_type(entry.first) != EncoderType::Jpeg)
        {
            continue;
        }

        jpeg_encoder_config_t &jpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoder_config);

        std::cout << "Encoder " << enc_i << " current quality: " << jpeg_encoder_config.quality << " Setting to "
                  << new_quality << std::endl;
        jpeg_encoder_config.quality = new_quality;
        bool profile_ret = set_override_parameters(profile);
        if (!profile_ret)
            return 1;
        enc_i++;
    }

    return 0;
}

int update_encoders_bitrate_monitor_period()
{
    uint32_t period = 2;
    uint enc_i = 0;
    auto expected_profile = m_media_lib->get_current_profile();
    if (!expected_profile.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return 1;
    }
    config_profile_t profile = expected_profile.value();
    for (auto &entry : profile.to_encoder_config_map())
    {
        encoder_config_t &encoder_config = entry.second;
        if (profile.get_encoder_type(entry.first) == EncoderType::Jpeg)
        {
            continue;
        }
        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        hailo_encoder_config.monitors_control.bitrate_monitor.period = period;
        std::cout << "Encoder " << enc_i << " setting bitrate monitor period to " << period << std::endl;
        bool profile_ret = set_override_parameters(profile);
        if (!profile_ret)
            return 1;
        enc_i++;
    }

    return 0;
}

int disable_encoders_bitrate_monitor()
{
    uint enc_i = 0;
    auto expected_profile = m_media_lib->get_current_profile();
    if (!expected_profile.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return 1;
    }
    config_profile_t profile = expected_profile.value();
    for (auto &entry : profile.to_encoder_config_map())
    {
        encoder_config_t &encoder_config = entry.second;

        if (profile.get_encoder_type(entry.first) == EncoderType::Jpeg)
        {
            continue;
        }
        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        hailo_encoder_config.monitors_control.bitrate_monitor.enable = false;
        std::cout << "Encoder " << enc_i << " disabling bitrate monitor" << std::endl;
        bool profile_ret = set_override_parameters(profile);
        if (!profile_ret)
            return 1;
        enc_i++;
    }

    return 0;
}

media_library_return toggle_frontend_config()
{
    auto profile_config_expected = m_media_lib->get_current_profile();
    if (!profile_config_expected.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    config_profile_t profile_config = profile_config_expected.value();
    profile_config.iq_settings.dewarp.enabled = false;
    std::cout << "Setting dewarp enable to false" << std::endl;
    bool profile_ret = set_override_parameters(profile_config);
    if (!profile_ret)
        return media_library_return::MEDIA_LIBRARY_ERROR;

    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds
    profile_config_expected = m_media_lib->get_current_profile();
    if (!profile_config_expected.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }
    profile_config = profile_config_expected.value();
    profile_config.iq_settings.dewarp.enabled = true;
    std::cout << "Setting dewarp enable to true" << std::endl;
    profile_ret = set_override_parameters(profile_config);
    if (!profile_ret)
        return media_library_return::MEDIA_LIBRARY_ERROR;
    profile_config = profile_config_expected.value();
    profile_config.iq_settings.dewarp.enabled = true;
    std::cout << "Setting dewarp enable to true" << std::endl;
    profile_ret = set_override_parameters(profile_config);
    if (!profile_ret)
        return media_library_return::MEDIA_LIBRARY_ERROR;

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

int main()
{
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        if (m_media_lib)
            m_media_lib->stop_pipeline();
        cleanup_resources();
        exit(signal);
    });

    std::string current_profile_name = "";
    m_user_profile = std::nullopt;
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_string = read_string_from_file("/usr/bin/medialib_config.json");
    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        return 1;

    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (streams.has_value())
    {
        for (auto s : streams.value())
        {
            std::string output_file_path = OUTPUT_FILE(s.id);
            m_output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        }
    }
    subscribe_elements(m_media_lib);

    m_media_lib->start_pipeline();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (toggle_frontend_config() != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to toggle frontend config" << std::endl;
    }
    if (update_encoders_bitrate() != 0)
    {
        return 1;
    }
    if (update_encoders_bitrate_monitor_period() != 0)
    {
        return 1;
    }
    if (update_jpeg_encoders_quality() != 0)
    {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    if (disable_encoders_bitrate_monitor() != 0)
    {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(10));
    m_media_lib->stop_pipeline();
    cleanup_resources();
    return 0;
}
