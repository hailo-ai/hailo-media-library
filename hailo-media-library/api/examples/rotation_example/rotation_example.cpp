#include "media_library/examples_common.hpp"

void set_rotation_degrees(MediaLibraryPtr media_library, rotation_angle_t rotation_angle)
{
    std::cout << "\n=== Rotation override via config profile (90 degrees) ===" << std::endl;

    if (!media_library)
    {
        std::cout << "ERROR: media_library is null" << std::endl;
        return;
    }

    // 1) Get current profile
    auto original_profile_exp = media_library->get_current_profile();
    if (!original_profile_exp.has_value())
    {
        return;
    }

    config_profile_t profile = original_profile_exp.value();
    std::cout << "Current profile: " << profile.name << std::endl;

    // 2) Enable rotation in application settings
    profile.application_settings.rotation.enabled = true;
    profile.application_settings.rotation.angle = rotation_angle;

    // 3) Swap encoder input width/height for all hailo encoders
    for (auto &encoded_stream : profile.encoded_output_streams)
    {
        // Check if it is a Hailo Video Encoder (H.264/H.265)
        if (std::holds_alternative<hailo_encoder_config_t>(encoded_stream.second.encoding))
        {
            auto &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoded_stream.second.encoding);
            auto &input_stream = hailo_encoder_config.input_stream;
            std::swap(input_stream.width, input_stream.height);
        }
        // Check if it is a JPEG Encoder
        else if (std::holds_alternative<jpeg_encoder_config_t>(encoded_stream.second.encoding))
        {
            auto &jpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoded_stream.second.encoding);
            auto &input_stream = jpeg_encoder_config.input_stream;
            std::swap(input_stream.width, input_stream.height);
        }
        // Skip other types
        else
        {
            continue;
        }
    }

    // 4) Apply profile override to the media library
    media_library_return ret = media_library->set_override_parameters(profile);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to override parameters for rotation" << std::endl;
        return;
    }

    // 5) Only fix text overlays at runtime:
    //    - Disable example_text1 / example_text2 from JSON
    //    - Add new text overlays with safe positions after rotation
    if (rotation_angle == ROTATION_ANGLE_90 || rotation_angle == ROTATION_ANGLE_270)
    {
        osd::rgba_color_t white = {255, 255, 255, 255};
        osd::rgba_color_t dark_red = {102, 0, 51, 255};
        osd::rgba_color_t black = {0, 0, 0, 255};

        std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";

        // Rotated "HailoAI" text overlay
        osd::TextOverlay text1_rotated("example_text1_rotated", // new overlay id
                                       0.05f,                   // x (normalized)
                                       0.05f,                   // y (normalized)
                                       "HailoAI",
                                       white, // text color
                                       black, // background / outline color
                                       40.0f, 1, 1, font_path, 0, osd::rotation_alignment_policy_t::CENTER);

        // Rotated "camera name" text overlay
        osd::TextOverlay text2_rotated("example_text2_rotated", 0.05f, 0.12f, "camera name", dark_red, black, 40.0f, 1,
                                       1, font_path, 0, osd::rotation_alignment_policy_t::CENTER);

        for (auto &entry : media_library->m_encoders)
        {
            auto encoder = entry.second;
            if (!encoder)
            {
                continue;
            }

            auto blender = encoder->get_osd_blender();
            if (!blender)
            {
                continue;
            }

            // Disable original JSON-based text overlays (they were computed
            // for the non-rotated resolution and can now go out-of-bounds).
            blender->set_overlay_enabled("example_text1", false);
            blender->set_overlay_enabled("example_text2", false);

            // Add and enable rotated text overlays
            blender->add_overlay(text1_rotated);
            blender->add_overlay(text2_rotated);

            blender->set_overlay_enabled("example_text1_rotated", true);
            blender->set_overlay_enabled("example_text2_rotated", true);
        }
    }
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

    m_user_profile = std::nullopt;
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_path = "/usr/bin/medialib_config.json";
    if (JPEG_SINK1)
    {
        medialib_config_path = "/usr/bin/medialib_config_jpeg.json";
    }

    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());

    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }

    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return 1;
    }
    for (auto s : streams.value())
    {
        std::string output_file_path = OUTPUT_FILE(s.id);
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

    std::cout << "Playing in default orientation for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // --- Perform Rotation ---
    std::cout << "Triggering Rotation to 90 degrees..." << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_90);

    std::cout << "Playing in rotated orientation for 15 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(15));

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();

    return 0;
}
