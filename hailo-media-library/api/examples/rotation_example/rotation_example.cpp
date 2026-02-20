#include "media_library/examples_common.hpp"

// Track current rotation state
static rotation_angle_t g_current_rotation = ROTATION_ANGLE_0;

int rotation_angle_to_degrees(rotation_angle_t angle)
{
    switch (angle)
    {
    case ROTATION_ANGLE_0:
        return 0;
    case ROTATION_ANGLE_90:
        return 90;
    case ROTATION_ANGLE_180:
        return 180;
    case ROTATION_ANGLE_270:
        return 270;
    default:
        return 0;
    }
}

bool is_rotated_90_or_270(rotation_angle_t angle)
{
    return (angle == ROTATION_ANGLE_90 || angle == ROTATION_ANGLE_270);
}

void disable_original_osd_and_add_rotated(MediaLibraryPtr media_lib)
{
    osd::rgba_color_t white = {255, 255, 255, 255};
    osd::rgba_color_t dark_red = {102, 0, 51, 255};
    osd::rgba_color_t black = {0, 0, 0, 255};

    std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";

    osd::TextOverlay text1_rotated("example_text1", 0.05f, 0.05f, "HailoAI", white, black, 40.0f, 1, 1, font_path, 0,
                                   osd::rotation_alignment_policy_t::CENTER);

    osd::TextOverlay text2_rotated("example_text2", 0.05f, 0.12f, "camera name", dark_red, black, 40.0f, 1, 1,
                                   font_path, 0, osd::rotation_alignment_policy_t::CENTER);

    for (auto &entry : media_lib->m_encoders)
    {
        auto encoder = entry.second;
        if (!encoder)
            continue;

        auto blender = encoder->get_osd_blender();
        if (!blender)
            continue;

        // Disable original overlays
        blender->set_overlay_enabled("example_text1", false);
        blender->set_overlay_enabled("example_text2", false);
        blender->set_overlay_enabled("example_image", false);
        blender->set_overlay_enabled("example_datetime", false);

        // Remove old rotated overlays first (in case they exist)
        blender->remove_overlay("example_text1");
        blender->remove_overlay("example_text2");

        // Add new rotated overlays
        blender->add_overlay(text1_rotated);
        blender->add_overlay(text2_rotated);

        blender->set_overlay_enabled("example_text1", true);
        blender->set_overlay_enabled("example_text2", true);
    }
}

void set_rotation_degrees(MediaLibraryPtr media_library, rotation_angle_t rotation_angle)
{
    std::cout << "\n=== Rotation override via config profile (" << rotation_angle_to_degrees(rotation_angle)
              << " degrees) ===" << std::endl;

    if (!media_library)
    {
        std::cout << "ERROR: media_library is null" << std::endl;
        return;
    }

    // Check if swap is needed based on previous and new rotation
    bool prev_is_90_270 = is_rotated_90_or_270(g_current_rotation);
    bool new_is_90_270 = is_rotated_90_or_270(rotation_angle);
    bool needs_swap = (prev_is_90_270 != new_is_90_270);

    // Get current profile
    auto original_profile_exp = media_library->get_current_profile();
    if (!original_profile_exp.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return;
    }

    config_profile_t profile = original_profile_exp.value();
    std::cout << "Current profile: " << profile.name << std::endl;

    // Enable rotation in application settings
    profile.application_settings.rotation.enabled = (rotation_angle != ROTATION_ANGLE_0);
    profile.application_settings.rotation.angle = rotation_angle;

    std::cout << "Setting rotation angle to: " << rotation_angle_to_degrees(rotation_angle) << " degrees" << std::endl;

    // Only swap width/height when transitioning between 0/180 and 90/270
    if (needs_swap)
    {
        std::cout << "Swapping encoder dimensions..." << std::endl;
        for (auto &encoded_stream : profile.encoded_output_streams)
        {
            if (std::holds_alternative<hailo_encoder_config_t>(encoded_stream.second.encoding))
            {
                auto &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoded_stream.second.encoding);
                auto &input_stream = hailo_encoder_config.input_stream;

                std::cout << "[" << encoded_stream.first << "] " << input_stream.width << "x" << input_stream.height
                          << " -> " << input_stream.height << "x" << input_stream.width << std::endl;
                std::swap(input_stream.width, input_stream.height);
            }
            else if (std::holds_alternative<jpeg_encoder_config_t>(encoded_stream.second.encoding))
            {
                auto &jpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoded_stream.second.encoding);
                auto &input_stream = jpeg_encoder_config.input_stream;

                std::cout << "[" << encoded_stream.first << " JPEG] " << input_stream.width << "x"
                          << input_stream.height << " -> " << input_stream.height << "x" << input_stream.width
                          << std::endl;
                std::swap(input_stream.width, input_stream.height);
            }
        }
    }
    else
    {
        std::cout << "No dimension swap needed" << std::endl;
    }

    // Apply profile override (only once!)
    media_library_return ret = media_library->set_override_parameters(profile);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to override parameters for rotation" << std::endl;
        return;
    }

    // Update current rotation state
    g_current_rotation = rotation_angle;

    std::cout << "Rotation to " << rotation_angle_to_degrees(rotation_angle) << " degrees applied successfully."
              << std::endl;

    // Handle OSD based on rotation angle
    std::cout << "Applying rotated OSD overlays..." << std::endl;
    disable_original_osd_and_add_rotated(media_library);
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

    std::string medialib_config_string = read_string_from_file(get_config_path().c_str());

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
        std::string output_file_path = OUTPUT_FILE_WITH_PREFIX("rotation", s.id);
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

    // --- Test Rotation Cycle ---
    std::cout << "\n>>> Rotating to 90 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_90);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "\n>>> Rotating to 180 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_180);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "\n>>> Rotating to 270 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_270);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "\n>>> Rotating to 0 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_0);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();

    return 0;
}
