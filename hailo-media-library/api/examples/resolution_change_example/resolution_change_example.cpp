#include "media_library/examples_common.hpp"

struct StreamResolution
{
    uint32_t width;
    uint32_t height;
};

void remove_all_osd(MediaLibraryPtr media_lib)
{
    for (auto &entry : media_lib->m_encoders)
    {
        auto encoder = entry.second;
        if (!encoder)
            continue;

        auto blender = encoder->get_osd_blender();
        if (!blender)
            continue;

        // Remove all overlays completely (not just disable)
        blender->remove_overlay("example_text1");
        blender->remove_overlay("example_text2");
        blender->remove_overlay("example_image");
        blender->remove_overlay("example_datetime");
        blender->remove_overlay("example_text1_changed");
        blender->remove_overlay("example_text2_changed");
        blender->remove_overlay("example_image_changed");
        blender->remove_overlay("example_datetime_changed");
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

    // IMPORTANT: Remove all OSD overlays BEFORE stopping pipeline
    // This prevents "overlay not ready to blend" errors during resolution change
    std::cout << "Removing OSD overlays before resolution change..." << std::endl;
    remove_all_osd(media_lib);

    // Wait for pending frames to be processed without OSD
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get the current profile
    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    config_profile_t profile = profile_exp.value();

    auto &encoded_streams = profile.encoded_output_streams;
    auto &app_inputs = profile.application_settings.application_input_streams.resolutions;

    size_t num_to_update = std::min({encoded_streams.size(), app_inputs.size(), new_resolutions.size()});

    if (num_to_update == 0)
    {
        std::cout << "Nothing to update." << std::endl;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    std::cout << "Updating " << num_to_update << " encoder(s)." << std::endl;

    auto encoded_it = encoded_streams.begin();
    for (size_t i = 0; i < num_to_update; ++i, ++encoded_it)
    {
        std::string stream_id = encoded_it->first;
        auto &encoded_stream = encoded_it->second;

        uint32_t new_w = new_resolutions[i].width;
        uint32_t new_h = new_resolutions[i].height;

        if (new_w == 0 || new_h == 0)
        {
            std::cout << "[" << stream_id << "] Skipping (0x0 requested)." << std::endl;
            continue;
        }

        std::cout << "[" << stream_id << "] Changing resolution to " << new_w << "x" << new_h << std::endl;

        uint32_t *current_w_ptr = nullptr;
        uint32_t *current_h_ptr = nullptr;

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

    std::cout << "Stopping pipeline for resolution change..." << std::endl;
    media_lib->stop_pipeline();

    std::cout << "Applying new resolution settings..." << std::endl;
    media_library_return ret = media_lib->set_override_parameters(profile);

    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to apply resolution override: " << static_cast<int>(ret) << std::endl;
        return ret;
    }

    std::cout << "Restarting pipeline..." << std::endl;

    // Wait for pipeline to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

    std::string medialib_config_string = read_string_from_file(get_config_path().c_str());

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
            std::string output_file_path = OUTPUT_FILE_WITH_PREFIX("resolution_change", s.id);
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
    low_res_settings.push_back({1920, 1080});
    low_res_settings.push_back({1280, 720});

    change_resolution(m_media_lib, low_res_settings);

    std::cout << "Playing with LOWER resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // TEST 2
    std::cout << "\n>>> TEST 2: Restoring Resolution to 3840x2160 (Both Streams) <<<" << std::endl;
    std::vector<StreamResolution> high_res_settings;
    high_res_settings.push_back({3840, 2160});
    high_res_settings.push_back({3840, 2160});

    change_resolution(m_media_lib, high_res_settings);

    std::cout << "Playing with RESTORED resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();

    return 0;
}
