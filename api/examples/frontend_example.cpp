#include "buffer_utils.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/privacy_mask_types.hpp"
#include "media_library/signal_utils.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <signal.h>

#define FRONTEND_CONFIG_FILE "/usr/bin/frontend_config_example.json"
#ifdef USE_JPEG_JSONS
// Use jpeg encoder only for second file
#define IS_JPEG(id) (id != "sink0")
#define FILE_ID(id) (IS_JPEG(id) ? "jpeg_" + id : id)
#else
#define FILE_ID(id) (id)
#define IS_JPEG(id) (false)

#endif
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(FILE_ID(id))
#define OUTPUT_FILE(id) get_output_file(FILE_ID(id), IS_JPEG(id))

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
};

std::shared_ptr<MediaLibrary> m_media_lib;

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

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
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

void subscribe_elements(std::shared_ptr<MediaLibrary> media_lib)
{
    auto streams = media_lib->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    FrontendCallbacksMap fe_callbacks;
    for (auto s : streams.value())
    {
        fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size) {
            media_lib->encoders[s.id]->add_buffer(buffer);
        };
    }
    media_lib->frontend->subscribe(fe_callbacks);

    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        media_lib->encoders[streamId]->subscribe([media_lib, streamId](HailoMediaLibraryBufferPtr buffer, size_t size) {
            write_encoded_data(buffer, size, media_lib->output_files[streamId]);
        });
    }
}

void add_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
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
    privacy_mask_blender->add_privacy_mask(example_polygon);

    polygon example_polygon2;
    example_polygon2.id = "privacy_mask2";
    example_polygon2.vertices.push_back(vertex(2500, 70));
    example_polygon2.vertices.push_back(vertex(2980, 70));
    example_polygon2.vertices.push_back(vertex(2900, 550));
    example_polygon2.vertices.push_back(vertex(2723, 550));
    example_polygon2.vertices.push_back(vertex(2600, 120));
    privacy_mask_blender->add_privacy_mask(example_polygon2);

    polygon example_polygon3;
    example_polygon3.id = "privacy_mask3";
    example_polygon3.vertices.push_back(vertex(400, 3160));
    example_polygon3.vertices.push_back(vertex(-100, 1860));
    example_polygon3.vertices.push_back(vertex(900, 1860));
    privacy_mask_blender->add_privacy_mask(example_polygon3);

    polygon example_polygon4;
    example_polygon4.id = "privacy_mask4";
    example_polygon4.vertices.push_back(vertex(3500, 50));
    example_polygon4.vertices.push_back(vertex(3600, -50));
    example_polygon4.vertices.push_back(vertex(3900, 550));
    example_polygon4.vertices.push_back(vertex(3800, 650));
    privacy_mask_blender->add_privacy_mask(example_polygon4);
}

int update_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    std::cout << "Updating privacy mask" << std::endl;
    auto polygon_exp = privacy_mask_blender->get_privacy_mask("privacy_mask1");
    if (!polygon_exp.has_value())
    {
        std::cout << "Failed to get privacy mask with id 'privacy_mask1'" << std::endl;
        return 1;
    }

    polygon polygon1 = polygon_exp.value();
    polygon1.vertices[0].x = 600;
    polygon1.vertices[0].y = 120;
    privacy_mask_blender->set_privacy_mask(polygon1);
    return 0;
}

int update_encoders_bitrate(std::map<output_stream_id_t, MediaLibraryEncoderPtr> &encoders)
{
    uint32_t new_bitrate = 15000000;
    uint enc_i = 0;
    for (const auto &entry : encoders)
    {
        encoder_config_t encoder_config = entry.second->get_user_config();
        if (entry.second->get_type() == EncoderType::Jpeg)
        {
            continue;
        }
        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        std::cout << "Encoder " << enc_i
                  << " current bitrate: " << hailo_encoder_config.rate_control.bitrate.target_bitrate << " Setting to "
                  << new_bitrate << std::endl;
        hailo_encoder_config.rate_control.bitrate.target_bitrate = new_bitrate;
        if (entry.second->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to configure Encoder " << enc_i << std::endl;
            return 1;
        }
        enc_i++;
    }
    return 0;
}

int update_jpeg_encoders_quality(std::map<output_stream_id_t, MediaLibraryEncoderPtr> &encoders)
{
    uint32_t new_quality = 75;
    uint enc_i = 0;
    for (const auto &entry : encoders)
    {
        encoder_config_t encoder_config = entry.second->get_user_config();
        if (entry.second->get_type() == EncoderType::Jpeg)
        {
            jpeg_encoder_config_t &jpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoder_config);

            std::cout << "Encoder " << enc_i << " current quality: " << jpeg_encoder_config.quality << " Setting to "
                      << new_quality << std::endl;
            jpeg_encoder_config.quality = new_quality;
            if (entry.second->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                std::cout << "Failed to configure Encoder " << enc_i << std::endl;
                return 1;
            }
        }
        enc_i++;
    }
    return 0;
}

int update_encoders_bitrate_monitor_period(std::map<output_stream_id_t, MediaLibraryEncoderPtr> &encoders,
                                           uint32_t period)
{
    uint enc_i = 0;
    for (const auto &entry : encoders)
    {
        encoder_config_t encoder_config = entry.second->get_user_config();
        if (entry.second->get_type() == EncoderType::Jpeg)
        {
            continue;
        }
        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        hailo_encoder_config.monitors_control.bitrate_monitor.period = period;
        if (entry.second->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to configure Encoder " << enc_i << std::endl;
            return 1;
        }
        enc_i++;
    }
    return 0;
}

int disable_encoders_bitrate_monitor(std::map<output_stream_id_t, MediaLibraryEncoderPtr> &encoders)
{
    uint enc_i = 0;
    for (const auto &entry : encoders)
    {
        encoder_config_t encoder_config = entry.second->get_user_config();
        if (entry.second->get_type() == EncoderType::Jpeg)
        {
            continue;
        }
        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        hailo_encoder_config.monitors_control.bitrate_monitor.enable = false;
        if (entry.second->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to configure Encoder " << enc_i << std::endl;
            return 1;
        }
        enc_i++;
    }
    return 0;
}

void stop_pipeline()
{
    std::cout << "Stopping Pipeline..." << std::endl;
    m_media_lib->frontend->stop();
    for (const auto &entry : m_media_lib->encoders)
    {
        entry.second->stop();
    }

    // close all file in m_media_lib->output_files
    for (auto &entry : m_media_lib->output_files)
    {
        entry.second.close();
    }
}

media_library_return toggle_frontend_config(MediaLibraryFrontendPtr frontend)
{
    auto config_expected = frontend->get_config();
    if (!config_expected)
    {
        std::cout << "Failed to get frontend config" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    frontend_config_t config = config_expected.value();

    config.ldc_config.dewarp_config.enabled = false;
    std::cout << "Setting dewarp enable to false" << std::endl;
    if (m_media_lib->frontend->set_config(config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to set frontend config" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    config.ldc_config.dewarp_config.enabled = true;
    std::cout << "Setting dewarp enable to true" << std::endl;
    if (m_media_lib->frontend->set_config(config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to set frontend config" << std::endl;
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

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

int main(int argc, char *argv[])
{
    m_media_lib = std::make_shared<MediaLibrary>();

    // register signal SIGINT and signal handler
    signal_utils::register_signal_handler([](int signal) {
        stop_pipeline();
        // terminate program
        exit(signal);
        ;
    });

    // Create and configure frontend
    std::string frontend_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected =
        MediaLibraryFrontend::create(FRONTEND_SRC_ELEMENT_V4L2SRC, frontend_config_string);
    if (!frontend_expected.has_value())
    {
        std::cout << "Failed to create frontend" << std::endl;
        return 1;
    }
    m_media_lib->frontend = frontend_expected.value();

    auto streams = m_media_lib->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    for (auto s : streams.value())
    {
        std::cout << "Creating encoder enc_" << s.id << std::endl;
        // Create and configure encoder
        std::string encoderosd_config_string = read_string_from_file(ENCODER_OSD_CONFIG_FILE(s.id).c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected =
            MediaLibraryEncoder::create(encoderosd_config_string, s.id);
        if (!encoder_expected.has_value())
        {
            std::cout << "Failed to create encoder osd" << std::endl;
            return 1;
        }
        m_media_lib->encoders[s.id] = encoder_expected.value();

        // create and configure output file
        std::string output_file_path = OUTPUT_FILE(s.id);
        delete_output_file(output_file_path);
        m_media_lib->output_files[s.id].open(output_file_path.c_str(),
                                             std::ios::out | std::ios::binary | std::ios::app);
        if (!m_media_lib->output_files[s.id].good())
        {
            std::cout << "Error occurred at writing time!" << std::endl;
            return 1;
        }
    }
    subscribe_elements(m_media_lib);

    std::cout << "Starting frontend." << std::endl;
    for (const auto &entry : m_media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        encoder->start();
    }
    m_media_lib->frontend->start();

    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    toggle_frontend_config(m_media_lib->frontend);

    add_custom_overlays(m_media_lib->encoders["sink0"]->get_blender());

    PrivacyMaskBlenderPtr privacy_blender = m_media_lib->frontend->get_privacy_mask_blender();
    add_privacy_masks(privacy_blender);

    std::cout << "Started playing for 30 seconds." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

    // Update privacy mask
    if (update_privacy_masks(privacy_blender) != 0)
    {
        stop_pipeline();
        return 1;
    }

    for (const auto &entry : m_media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        std::cout << "Current fps for " << streamId << " is " << encoder->get_current_fps() << std::endl;
    }

    auto output_streams_current_fps = m_media_lib->frontend->get_output_streams_current_fps();
    for (auto &[output_stream_id, output_stream_current_fps] : output_streams_current_fps)
    {
        std::cout << "Current fps for frontend output id " << output_stream_id << " is " << output_stream_current_fps
                  << std::endl;
    }

    if (update_encoders_bitrate(m_media_lib->encoders) != 0)
        return 1;

    if (update_encoders_bitrate_monitor_period(m_media_lib->encoders, 2) != 0)
        return 1;

    if (update_jpeg_encoders_quality(m_media_lib->encoders) != 0)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep for 2 seconds

    if (disable_encoders_bitrate_monitor(m_media_lib->encoders) != 0)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds

    stop_pipeline();

    return 0;
}
