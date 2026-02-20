#include "media_library/examples_common.hpp"

void update_osd_profile_name(const std::string &profile_name)
{
    if (m_media_lib->get_pipeline_state() != media_library_pipeline_state_t::PIPELINE_STATE_RUNNING)
    {
        std::cout << "Pipeline is not started, skipping OSD update" << std::endl;
        return;
    }

    for (auto &encoder : m_media_lib->m_encoders)
    {
        auto blender = encoder.second->get_osd_blender();
        auto overlay_exp = blender->get_overlay("profile_text_overlay");
        if (overlay_exp.has_value())
        {
            std::shared_ptr<osd::TextOverlay> overlay = std::static_pointer_cast<osd::TextOverlay>(overlay_exp.value());
            overlay->label = "Profile: " + profile_name;
            blender->set_overlay(*overlay);
        }
    }
}

bool set_profile(const std::string &profile_name)
{
    if (!m_media_lib)
    {
        std::cout << "m_media_lib is null" << std::endl;
        return false;
    }

    media_library_return profile_ret = m_media_lib->set_profile(profile_name);
    if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        if (profile_ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
        {
            std::cout << "Profile is restricted at this moment, skipping" << std::endl;
        }
        else
        {
            std::cout << "Failed to set profile to " << profile_name << std::endl;
            return false;
        }
    }

    auto get_profile_exp = m_media_lib->get_profile(profile_name);
    if (!get_profile_exp.has_value())
    {
        std::cout << "Failed to get profile " << profile_name << std::endl;
        return false;
    }

    config_profile_t target_profile = get_profile_exp.value();

    m_user_profile = target_profile;
    update_osd_profile_name(profile_name);

    return true;
}

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

    update_osd_profile_name(m_user_profile.value().name);
    return true;
}

int main()
{
    std::string current_profile_name = "";
    m_user_profile = std::nullopt;
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_string = read_string_from_file(get_config_path().c_str());

    m_media_lib->set_override_persistent_settings(true);

    m_media_lib->subscribe_to_profile_restricted(
        [](const config_profile_t &previous_profile, const config_profile_t &new_profile) {
            std::cout << "Profile restricted - previous profile denoise enabled: "
                      << previous_profile.iq_settings.denoise.enabled
                      << " new profile denoise enabled: " << new_profile.iq_settings.denoise.enabled << std::endl;
            m_user_profile = previous_profile;
            std::string current_profile_name = "";
            auto get_profile_exp = m_media_lib->get_current_profile();
            if (get_profile_exp.has_value())
            {
                current_profile_name = new_profile.name;
            }
            else
            {
                std::cout << "Failed to get profile name" << std::endl;
            }

            if (previous_profile.iq_settings.denoise.enabled && !new_profile.iq_settings.denoise.enabled)
            {
                update_osd_profile_name(current_profile_name + " - denoise disabled");
            }
            else
            {
                update_osd_profile_name(current_profile_name);
            }
        });

    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }

    m_media_lib->subscribe_to_profile_restriction_done([]() {
        std::cout << "Profile restriction done" << std::endl;
        // Set profile to previous profile?
        if (m_user_profile.has_value())
        {
            std::cout << "Setting profile to previous restricted profile" << std::endl;
            config_profile_t &restricted_profile = m_user_profile.value();
            bool ret = set_override_parameters(restricted_profile);
            if (!ret)
                std::cout << "Failed to set profile to previous restricted profile" << std::endl;

            std::string profile_name = restricted_profile.name;
            if (!restricted_profile.iq_settings.denoise.enabled)
            {
                profile_name += " - denoise disabled";
            }
            update_osd_profile_name(profile_name);
        }
    });

    m_media_lib->subscribe_to_pipeline_state_change([](media_library_pipeline_state_t state) {
        switch (state)
        {
        case media_library_pipeline_state_t::PIPELINE_STATE_STOPPED:
            std::cout << "Pipeline stopped" << std::endl;
            break;
        case media_library_pipeline_state_t::PIPELINE_STATE_RUNNING:
            std::cout << "Pipeline running" << std::endl;
            break;
        default:
            break;
        }
    });

    auto get_profile_exp = m_media_lib->get_current_profile();
    if (!get_profile_exp.has_value())
    {
        std::cout << "Failed to get profile name" << std::endl;
        return 1;
    }
    current_profile_name = get_profile_exp.value().name;

    osd::rgba_color_t red_argb = {255, 0, 0, 255};
    osd::rgba_color_t blue_argb = {0, 0, 255, 255};
    std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
    osd::TextOverlay profile_text_overlay("profile_text_overlay", 0.1, 0.4, "Current Profile: " + current_profile_name,
                                          red_argb, blue_argb, 40.0f, 1, 1, font_path, 0,
                                          osd::rotation_alignment_policy_t::CENTER);

    for (const auto &encoder : m_media_lib->m_encoders)
    {
        encoder.second->get_osd_blender()->add_overlay(profile_text_overlay);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1)); // let overlays be added before enabling
    for (const auto &encoder : m_media_lib->m_encoders)
    {
        encoder.second->get_osd_blender()->set_overlay_enabled("profile_text_overlay", true);
    }

    // register signal SIGINT and signal handler
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        m_media_lib->stop_pipeline();
        cleanup_resources();
        exit(signal);
    });
    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return 1;
    }
    for (auto s : streams.value())
    {
        std::string output_file_path = OUTPUT_FILE_WITH_PREFIX("profile_switching", s.id);
        delete_output_file(output_file_path);
        m_output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        if (!m_output_files[s.id].good())
        {
            std::cout << "Error occurred at writing time!" << std::endl;
            return 1;
        }
    }
    subscribe_elements(m_media_lib);

    std::cout << "Starting frontend." << std::endl;
    if (m_media_lib->start_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to start frontend" << std::endl;
        return 1;
    }

    if (!JPEG_SINK1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to HDR" << std::endl;

        bool profile_ret = set_profile("High_Dynamic_Range_Basic");
        if (!profile_ret)
            return 1;

        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to low light" << std::endl;

        if (!set_profile("Lowlight_Basic"))
        {
            std::cout << "Failed to set profile" << std::endl;
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to day light" << std::endl;

        if (!set_profile("Daylight_Basic"))
        {
            std::cout << "Failed to set profile" << std::endl;
            return 1;
        }
    }

    else
    {
        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to HDR JPEG" << std::endl;

        bool profile_ret = set_profile("High_Dynamic_Range_JPEG");
        if (!profile_ret)
            return 1;

        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to low light JPEG" << std::endl;

        if (!set_profile("Lowlight_JPEG"))
        {
            std::cout << "Failed to set profile" << std::endl;
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to day light JPEG" << std::endl;

        if (!set_profile("Daylight_JPEG"))
        {
            std::cout << "Failed to set profile" << std::endl;
            return 1;
        }
    }

    if (SWITCH_ENCODER_EXAMPLE)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to day light_Jpeg" << std::endl;

        if (!set_profile("Daylight_JPEG"))
        {
            std::cout << "Failed to set profile" << std::endl;
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to HDR_JPEG" << std::endl;

        bool profile_ret_jpeg = set_profile("High_Dynamic_Range_JPEG");
        if (!profile_ret_jpeg)
            return 1;

        std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

        std::cout << "Setting profile to low light_JPEG" << std::endl;

        if (!set_profile("Lowlight_JPEG"))
        {
            std::cout << "Failed to set profile" << std::endl;
            return 1;
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 5 seconds
    std::cout << "Stopping pipeline." << std::endl;
    if (m_media_lib->stop_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Pipeline failed to stop." << std::endl;
        std::cout << "Failed to stop pipeline" << std::endl;
        return 1;
    }

    cleanup_resources();

    return 0;
}
