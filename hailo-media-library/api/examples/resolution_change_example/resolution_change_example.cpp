#include "media_library/examples_common.hpp"

struct StreamResolution
{
    uint32_t width;
    uint32_t height;
};

void reset_osd(MediaLibraryPtr media_lib)
{
    osd::rgba_color_t white = {255, 255, 255, 255};
    osd::rgba_color_t dark_red = {102, 0, 51, 255};
    osd::rgba_color_t black = {0, 0, 0, 255};

    std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
    std::string image_path = "/home/root/apps/webserver/resources/configs/osd_hailo_static_image.png";

    osd::TextOverlay text1_changed("example_text1_changed", // new overlay id
                                   0.05f,                   // x (normalized)
                                   0.05f,                   // y (normalized)
                                   "HailoAI",
                                   white, // text color
                                   black, // background / outline color
                                   40.0f, 1, 1, font_path, 0, osd::rotation_alignment_policy_t::CENTER);

    osd::TextOverlay text2_changed("example_text2_changed", 0.05f, 0.05f, "camera name", dark_red, black, 40.0f, 1, 1,
                                   font_path, 0, osd::rotation_alignment_policy_t::CENTER);

    osd::ImageOverlay image_changed("example_image_changed", 0.05f, 0.05f, 0.1, 0.1, image_path, 1, 0,
                                    osd::rotation_alignment_policy_t::CENTER);

    osd::DateTimeOverlay datetime_changed("example_datetime_changed", 0.05f, 0.05f, black, 60.0f, 1, 1, 0,
                                          osd::rotation_alignment_policy_t::CENTER);

    for (auto &entry : media_lib->m_encoders)
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
        blender->set_overlay_enabled("example_image", false);
        blender->set_overlay_enabled("example_datetime", false);

        // Add and enable rotated text overlays
        blender->add_overlay(text1_changed);
        blender->add_overlay(text2_changed);
        blender->add_overlay(image_changed);
        blender->add_overlay(datetime_changed);

        blender->set_overlay_enabled("example_text1_changed", true);
        blender->set_overlay_enabled("example_text2_changed", true);
        blender->set_overlay_enabled("example_image_changed", true);
        blender->set_overlay_enabled("example_datetime_changed", true);
    }
}

media_library_return change_resolution(MediaLibraryPtr media_lib, const std::vector<StreamResolution> &new_resolutions)
{
    if (!media_lib)
    {
        std::cout << "ERROR: media_lib is null" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    std::cout << "\n=== Resolution change (Dynamic Vector Input) ===" << std::endl;

    // Get the current profile
    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    config_profile_t profile = profile_exp.value();

    auto &encoded_streams = profile.encoded_output_streams;                                // std::map
    auto &app_inputs = profile.application_settings.application_input_streams.resolutions; // std::vector

    // Determine how many items to update based on the smallest size
    size_t num_to_update = std::min({encoded_streams.size(), app_inputs.size(), new_resolutions.size()});

    if (num_to_update == 0)
    {
        std::cout << "Nothing to update." << std::endl;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    std::cout << "Updating " << num_to_update << " encoder(s)." << std::endl;

    // Use an iterator to traverse the map
    auto encoded_it = encoded_streams.begin();
    for (size_t i = 0; i < num_to_update; ++i, ++encoded_it)
    {

        std::string stream_id = encoded_it->first;
        auto &encoded_stream = encoded_it->second;

        uint32_t new_w = new_resolutions[i].width;
        uint32_t new_h = new_resolutions[i].height;

        // "0" means skip
        if (new_w == 0 || new_h == 0)
        {
            std::cout << "[" << stream_id << "] Skipping (0x0 requested)." << std::endl;
            continue;
        }

        std::cout << "[" << stream_id << "] Changing resolution to " << new_w << "x" << new_h << std::endl;

        uint32_t *current_w_ptr = nullptr;
        uint32_t *current_h_ptr = nullptr;

        // Check Encoder Type
        if (std::holds_alternative<hailo_encoder_config_t>(encoded_stream.encoding))
        {
            auto &hailo_cfg = std::get<hailo_encoder_config_t>(encoded_stream.encoding);
            current_w_ptr = &hailo_cfg.input_stream.width;
            current_h_ptr = &hailo_cfg.input_stream.height;
        }
        else if (std::holds_alternative<jpeg_encoder_config_t>(encoded_stream.encoding))
        {
            auto &jpeg_cfg = std::get<jpeg_encoder_config_t>(encoded_stream.encoding);
            current_w_ptr = &jpeg_cfg.input_stream.width;
            current_h_ptr = &jpeg_cfg.input_stream.height;
        }
        else
        {
            std::cout << "Unsupported encoder type." << std::endl;
            continue;
        }

        // Perform the update using the pointers we set above
        if (current_w_ptr && current_h_ptr)
        {
            std::cout << "[" << stream_id << " Encoder] " << *current_w_ptr << "x" << *current_h_ptr << " -> " << new_w
                      << "x" << new_h << std::endl;
            *current_w_ptr = new_w;
            *current_h_ptr = new_h;
        }

        auto app_it = std::find_if(app_inputs.begin(), app_inputs.end(),
                                   [&stream_id](const auto &input) { return input.stream_id == stream_id; });

        if (app_it != app_inputs.end())
        {
            std::cout << "[" << stream_id << " AppInput] " << app_it->dimensions.destination_width << "x"
                      << app_it->dimensions.destination_height << " -> " << new_w << "x" << new_h << std::endl;

            app_it->dimensions.destination_width = new_w;
            app_it->dimensions.destination_height = new_h;
        }
        else
        {
            std::cout << "Warning: Could not find app input for stream " << stream_id << std::endl;
        }
    }

    // Apply the updated profile
    media_library_return ret = media_lib->set_override_parameters(profile);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to apply resolution override: " << static_cast<int>(ret) << std::endl;
        return ret;
    }

    reset_osd(media_lib);

    std::cout << "Resolution override applied successfully." << std::endl;
    return ret;
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
        medialib_config_path = "/usr/bin/medialib_config_jpeg.json";

    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());

    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }

    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (streams.has_value())
    {
        for (auto s : streams.value())
        {
            std::string output_file_path = OUTPUT_FILE(s.id);
            delete_output_file(output_file_path);
            m_output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        }
    }
    subscribe_elements(m_media_lib);

    std::cout << "Starting frontend." << std::endl;
    if (m_media_lib->start_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to start frontend" << std::endl;
        return 1;
    }

    std::cout << "Playing with ORIGINAL resolution for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // TEST 1
    std::cout << "\n>>> TEST 1: Changing Resolution to 1920x1080 (Stream 0) and 1280x720 (Stream 1) <<<" << std::endl;
    std::vector<StreamResolution> low_res_settings;
    low_res_settings.push_back({1920, 1080}); // Target for Stream 0 (sink0)
    low_res_settings.push_back({1280, 720});  // Target for Stream 1 (sink1, if exists)

    change_resolution(m_media_lib, low_res_settings);

    std::cout << "Playing with LOWER resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // TEST 2
    std::cout << "\n>>> TEST 2: Restoring Resolution to 3840x2160 (Both Streams) <<<" << std::endl;
    std::vector<StreamResolution> high_res_settings;
    high_res_settings.push_back({3840, 2160}); // Target for Stream 0
    high_res_settings.push_back({0, 0});       // Skip

    change_resolution(m_media_lib, high_res_settings);

    std::cout << "Playing with RESTORED resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();

    return 0;
}
