#include "buffer_utils.hpp"
#include "media_library/media_library.hpp"
#include "media_library/utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/privacy_mask_types.hpp"
#include "media_library/signal_utils.hpp"
#include <fstream>
#include <optional>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <signal.h>
#define JPEG_SINK1 false

#ifdef USE_JPEG_JSONS
#undef JPEG_SINK1 // Undefine the previous definition
#define JPEG_SINK1 true
// Use jpeg encoder only for second file
#define IS_JPEG(id) (id != "sink0")
#define FILE_ID(id) (IS_JPEG(id) ? "jpeg_" + id : id)
#else
#define FILE_ID(id) (id)
#define IS_JPEG(id) (false)

#endif
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(FILE_ID(id))
#define OUTPUT_FILE(id) get_output_file(FILE_ID(id), IS_JPEG(id))

MediaLibraryPtr m_media_lib;
std::map<output_stream_id_t, std::ofstream> m_output_files;
std::optional<config_profile_t> m_user_profile;

inline std::string get_encoder_osd_config_file(const std::string &id)
{
    return "/usr/bin/frontend_encoder_" + id + ".json";
}

inline std::string get_output_file(const std::string &id, bool is_jpeg)
{
    std::string suffix = (is_jpeg ? ".jpegenc" : ".h264");
    return "/var/volatile/tmp/frontend_example_" + id + suffix;
}

void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
{
    char *data = (char *)buffer->get_plane_ptr(0);
    if (!data)
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    output_file.write(data, size);
}

void delete_output_file(std::string output_file)
{
    std::ofstream fp(output_file.c_str(), std::ios::out | std::ios::binary);
    if (!fp.good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    fp.close();
}

void subscribe_elements(MediaLibraryPtr media_lib)
{
    auto streams = media_lib->m_frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    FrontendCallbacksMap fe_callbacks;
    for (auto s : streams.value())
    {
        fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t) {
            media_lib->m_encoders[s.id]->add_buffer(buffer);
        };
    }
    media_lib->subscribe_to_frontend_output(fe_callbacks);

    for (const auto &entry : media_lib->m_encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        AppWrapperCallback callback = [streamId, encoder](HailoMediaLibraryBufferPtr buffer, size_t size) {
            write_encoded_data(buffer, size, m_output_files[streamId]);
        };
        media_lib->subscribe_to_encoder_output(streamId, callback);
    }
}

void add_static_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    polygon example_polygon;
    example_polygon.id = "privacy_mask1";
    example_polygon.vertices.push_back(vertex(125, 40));
    example_polygon.vertices.push_back(vertex(980, 40));
    example_polygon.vertices.push_back(vertex(1020, 600));
    example_polygon.vertices.push_back(vertex(1350, 920));
    example_polygon.vertices.push_back(vertex(750, 750));
    example_polygon.vertices.push_back(vertex(125, 920));
    example_polygon.vertices.push_back(vertex(250, 600));
    privacy_mask_blender->add_static_privacy_mask(example_polygon);

    polygon example_polygon2;
    example_polygon2.id = "privacy_mask2";
    example_polygon2.vertices.push_back(vertex(2500, 70));
    example_polygon2.vertices.push_back(vertex(2980, 70));
    example_polygon2.vertices.push_back(vertex(2900, 550));
    example_polygon2.vertices.push_back(vertex(2723, 550));
    example_polygon2.vertices.push_back(vertex(2600, 120));
    privacy_mask_blender->add_static_privacy_mask(example_polygon2);

    polygon example_polygon3;
    example_polygon3.id = "privacy_mask3";
    example_polygon3.vertices.push_back(vertex(400, 3160));
    example_polygon3.vertices.push_back(vertex(-100, 1860));
    example_polygon3.vertices.push_back(vertex(900, 1860));
    privacy_mask_blender->add_static_privacy_mask(example_polygon3);

    polygon example_polygon4;
    example_polygon4.id = "privacy_mask4";
    example_polygon4.vertices.push_back(vertex(3500, 50));
    example_polygon4.vertices.push_back(vertex(3600, -50));
    example_polygon4.vertices.push_back(vertex(3900, 550));
    example_polygon4.vertices.push_back(vertex(3800, 650));
    privacy_mask_blender->add_static_privacy_mask(example_polygon4);
}

void change_to_pixelization_and_back_to_color(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds
    std::cout << "changing privacy masks to pixelization" << std::endl;
    privacy_mask_blender->set_pixelization_size(60);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "changing size of pixelization" << std::endl;
    privacy_mask_blender->set_pixelization_size(10);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "changing privacy masks to color" << std::endl;
    privacy_mask_blender->set_color({23, 161, 231});
}

int update_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    std::cout << "Updating privacy mask" << std::endl;
    auto polygon_exp = privacy_mask_blender->get_static_privacy_mask("privacy_mask1");
    if (!polygon_exp.has_value())
    {
        std::cout << "Failed to get privacy mask with id 'privacy_mask1'" << std::endl;
        return 1;
    }

    polygon polygon1 = polygon_exp.value();
    polygon1.vertices[0].x = 600;
    polygon1.vertices[0].y = 120;
    privacy_mask_blender->set_static_privacy_mask(polygon1);

    change_to_pixelization_and_back_to_color(privacy_mask_blender);
    return 0;
}

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

    m_user_profile = get_profile_exp.value();
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

void cleanup_resources()
{
    sleep(2);
    m_media_lib->m_frontend = nullptr;
    m_media_lib->m_encoders.clear();

    // close all file in m_media_lib->output_files
    for (auto &entry : m_output_files)
    {
        entry.second.close();
    }

    m_output_files.clear();
    m_media_lib = nullptr;
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

media_library_return add_custom_overlays(std::shared_ptr<osd::Blender> blender)
{
    osd::CustomOverlay custom_overlay("custom_argb", 0.3, 0.5, 0.1, 0.1, 1, osd::custom_overlay_format::ARGB);
    blender->add_overlay(
        custom_overlay); // this adds the overlay but does not show it yet, we need to set it as ready as shown below
    auto custom_expected = blender->get_overlay("custom_argb");
    auto existing_custom_overlay = std::static_pointer_cast<osd::CustomOverlay>(custom_expected.value());
    HailoMediaLibraryBufferPtr hailo_argb_buffer = existing_custom_overlay->get_buffer();
    void *plane0_userptr = hailo_argb_buffer->get_plane_ptr(0);
    for (size_t i = 0; i < hailo_argb_buffer->get_plane_size(0); i += 4)
    {
        ((char *)plane0_userptr)[i] = 0x80;     // Alpha: 80 (half opaque)
        ((char *)plane0_userptr)[i + 1] = 0x00; // Red: 00 (no intensity)
        ((char *)plane0_userptr)[i + 2] = 0x00; // Green: 00 (no intensity)
        ((char *)plane0_userptr)[i + 3] = 0xFF; // Blue: FF (full intensity)
    }

    std::cout << "Enable custom overlay" << std::endl;
    blender->set_overlay_enabled("custom_argb", true);
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    std::cout << "Disable custom overlay " << std::endl;
    blender->set_overlay_enabled("custom_argb", false);
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    std::cout << "Enable custom overlay" << std::endl;
    blender->set_overlay_enabled("custom_argb", true);
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    // add another custom overlay but with format A420
    osd::CustomOverlay custom_overlay2("custom_a420", 0.7, 0.7, 0.1, 0.1, 1, osd::custom_overlay_format::A420);
    blender->add_overlay(
        custom_overlay2); // this adds the overlay but does not show it yet, we need to set it as ready as shown below
    auto custom_expected2 = blender->get_overlay("custom_a420");
    auto existing_custom_overlay2 = std::static_pointer_cast<osd::CustomOverlay>(custom_expected2.value());
    HailoMediaLibraryBufferPtr hailo_a420_buffer = existing_custom_overlay2->get_buffer();

    uint8_t blue_y = 29, blue_u = 255, blue_v = 107, blue_a = 128;
    memset(hailo_a420_buffer->get_plane_ptr(0), blue_y, hailo_a420_buffer->get_plane_size(0));
    memset(hailo_a420_buffer->get_plane_ptr(1), blue_u, hailo_a420_buffer->get_plane_size(1));
    memset(hailo_a420_buffer->get_plane_ptr(2), blue_v, hailo_a420_buffer->get_plane_size(2));
    memset(hailo_a420_buffer->get_plane_ptr(3), blue_a, hailo_a420_buffer->get_plane_size(3));

    std::cout << "Enable custom overlay" << std::endl;
    blender->set_overlay_enabled("custom_a420", true);

    return media_library_return::MEDIA_LIBRARY_SUCCESS;
}

int main()
{
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

    auto get_profile_exp = m_media_lib->get_current_profile();
    std::string current_profile_name = "";

    osd::rgba_color_t red_argb = {255, 0, 0, 255};
    osd::rgba_color_t blue_argb = {0, 0, 255, 255};
    std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
    osd::TextOverlay profile_text_overlay("profile_text_overlay", 0.1, 0.4, "Current Profile: " + current_profile_name,
                                          red_argb, blue_argb, 40.0f, 1, 1, font_path, 0,
                                          osd::rotation_alignment_policy_t::CENTER);
    for (const auto &encoder : m_media_lib->m_encoders)
    {
        encoder.second->get_osd_blender()->add_overlay(profile_text_overlay);
        encoder.second->get_osd_blender()->set_overlay_enabled("profile_text_overlay", true);
    }

    m_media_lib->on_profile_restricted([](config_profile_t previous_profile, config_profile_t new_profile) {
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

    m_media_lib->on_profile_restriction_done([]() {
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

    m_media_lib->on_pipeline_state_change([](media_library_pipeline_state_t state) {
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

    get_profile_exp = m_media_lib->get_current_profile();
    if (!get_profile_exp.has_value())
    {
        std::cout << "Failed to get profile name" << std::endl;
        return 1;
    }
    current_profile_name = get_profile_exp.value().name;
    update_osd_profile_name(current_profile_name);

    // register signal SIGINT and signal handler
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        m_media_lib->stop_pipeline();
        cleanup_resources();
        exit(signal);
    });
    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    for (auto s : streams.value())
    {
        // create and configure output file
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

    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    if (toggle_frontend_config() != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to toggle frontend config" << std::endl;
    }

    add_custom_overlays(m_media_lib->m_encoders["sink0"]->get_osd_blender());

    PrivacyMaskBlenderPtr privacy_blender = m_media_lib->m_encoders["sink0"]->get_privacy_mask_blender();
    add_static_privacy_masks(privacy_blender);

    std::cout << "Started playing for 30 seconds." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

    // Update privacy mask
    if (update_privacy_masks(privacy_blender) != 0)
    {
        m_media_lib->stop_pipeline();
        cleanup_resources();
        return 1;
    }

    for (const auto &entry : m_media_lib->m_encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        std::cout << "Current fps for " << streamId << " is " << encoder->get_current_fps() << std::endl;
    }

    auto output_streams_current_fps = m_media_lib->m_frontend->get_output_streams_current_fps();
    for (auto &[output_stream_id, output_stream_current_fps] : output_streams_current_fps)
    {
        std::cout << "Current fps for frontend output id " << output_stream_id << " is " << output_stream_current_fps
                  << std::endl;
    }

    if (update_encoders_bitrate() != 0)
        return 1;

    if (update_encoders_bitrate_monitor_period() != 0)
        return 1;

    if (update_jpeg_encoders_quality() != 0)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    if (disable_encoders_bitrate_monitor() != 0)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

    std::cout << "Setting profile to HDR" << std::endl;

    bool profile_ret = set_profile("High_Dynamic_Range");
    if (!profile_ret)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

    std::cout << "Setting profile to low light" << std::endl;

    if (!set_profile("Lowlight"))
    {
        std::cout << "Failed to set profile" << std::endl;
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

    std::cout << "Setting profile to day light" << std::endl;

    if (!set_profile("Daylight"))
    {
        std::cout << "Failed to set profile" << std::endl;
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(5)); // sleep for 5 seconds

    if (m_media_lib->stop_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to stop pipeline" << std::endl;
        return 1;
    }
    cleanup_resources();

    return 0;
}
