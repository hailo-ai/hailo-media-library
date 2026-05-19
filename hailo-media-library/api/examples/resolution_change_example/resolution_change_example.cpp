#include <stdint.h>
#include <stdlib.h>
#include <tl/expected.hpp>
#include <map>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "common/common.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/dsp_utils.hpp"
#include "media_library/encoder_config_types.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library.hpp"
#include "media_library/utils.hpp"
#include "cloexec_fstream.hpp"

struct StreamResolution
{
    uint32_t width;
    uint32_t height;
};

media_library_return change_resolution(MediaLibraryPtr media_lib, const std::vector<StreamResolution> &new_resolutions)
{
    if (!media_lib)
    {
        std::cout << "ERROR: media_lib is null" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    std::cout << "\n=== Resolution change ===" << std::endl;

    // Get the current profile
    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    config_profile_t profile = profile_exp.value();

    // Save OSD configs and temporarily remove them before changing resolution.
    // OSD overlays are positioned using pixel coordinates tied to the current
    // resolution. If we change the resolution while overlays are still active,
    // their positions become invalid for the new frame size, causing a crash.
    // After the resolution change completes, we restore the OSD so overlays
    // are recreated with correct positions for the new resolution.
    std::map<std::string, config_stream_osd_t> saved_osd;
    for (auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
    {
        saved_osd[stream_id] = encoded_stream.osd;
        encoded_stream.osd = config_stream_osd_t{};
    }

    auto &app_inputs = profile.application_settings.application_input_streams.resolutions;
    size_t num_to_update = std::min({profile.encoded_output_streams.size(), app_inputs.size(), new_resolutions.size()});

    if (num_to_update == 0)
    {
        std::cout << "Nothing to update." << std::endl;
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    }

    std::cout << "Updating " << num_to_update << " encoder(s)." << std::endl;

    auto encoded_it = profile.encoded_output_streams.begin();
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

        uint32_t previous_encoder_height = current_h_ptr ? *current_h_ptr : 0;

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

        // Scale saved OSD by the height ratio so overlays match the new resolution.
        if (previous_encoder_height > 0 && new_h != previous_encoder_height)
        {
            auto saved_it = saved_osd.find(stream_id);
            if (saved_it != saved_osd.end())
            {
                const double scale = static_cast<double>(new_h) / static_cast<double>(previous_encoder_height);
                examples::scale_osd_pixel_fields(saved_it->second, scale);
            }
        }
    }

    // Phase 1: Stop pipeline and apply resolution change with OSD cleared.
    // With overlays removed from the config, the resolution change can proceed
    // safely without the blender trying to render overlays at invalid positions.
    std::cout << "Stopping pipeline for resolution change..." << std::endl;
    media_lib->stop_pipeline();

    std::cout << "Applying new resolution (OSD temporarily cleared)..." << std::endl;
    media_library_return ret = media_lib->set_override_parameters(profile);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to apply resolution override: " << static_cast<int>(ret) << std::endl;
        return ret;
    }

    // Wait for pipeline to restart and stabilize at new resolution
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Phase 2: Restore OSD config and re-apply. Overlays will be created fresh
    // by the blender at the new resolution with correct pixel offsets.
    std::cout << "Restoring OSD overlays at new resolution..." << std::endl;
    for (auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
    {
        auto osd_it = saved_osd.find(stream_id);
        if (osd_it != saved_osd.end())
        {
            encoded_stream.osd = osd_it->second;
        }
    }

    ret = media_lib->set_override_parameters(profile);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to restore OSD: " << static_cast<int>(ret) << std::endl;
        return ret;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "Resolution override applied successfully." << std::endl;
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

    examples::scale_osd_to_output_resolution(m_media_lib);

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
    std::cout << "\n>>> TEST 1: Changing Resolution to 1920x1080 (Stream 0) and 960x540 (Stream 1) <<<" << std::endl;
    std::vector<StreamResolution> mid_res_settings;
    mid_res_settings.push_back({1920, 1080});
    mid_res_settings.push_back({960, 540});

    change_resolution(m_media_lib, mid_res_settings);

    std::cout << "Playing with MID resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // TEST 2
    std::cout << "\n>>> TEST 2: Changing Resolution to 1280x720 (Stream 0) and 640x360 (Stream 1) <<<" << std::endl;
    std::vector<StreamResolution> low_res_settings;
    low_res_settings.push_back({1280, 720});
    low_res_settings.push_back({640, 360});

    change_resolution(m_media_lib, low_res_settings);

    std::cout << "Playing with LOWER resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // TEST 3
    std::cout << "\n>>> TEST 3: Restoring Resolution to 3840x2160 (Stream 0) and 1280x720 (Stream 1) <<<" << std::endl;
    std::vector<StreamResolution> high_res_settings;
    high_res_settings.push_back({3840, 2160});
    high_res_settings.push_back({1280, 720});

    change_resolution(m_media_lib, high_res_settings);

    std::cout << "Playing with RESTORED resolution for 10 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();

    return 0;
}
