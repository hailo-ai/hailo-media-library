#include "buffer_utils.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/privacy_mask_types.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>

#define FRONTEND_CONFIG_FILE "/usr/bin/frontend_config_example.json"
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(id)
#define OUTPUT_FILE(id) get_output_file(id)

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
};

inline std::string get_encoder_osd_config_file(const std::string &id)
{
    return "/usr/bin/frontend_encoder_" + id + ".json";
}

inline std::string get_output_file(const std::string &id)
{
    return "/var/volatile/tmp/frontend_example_" + id + ".h264";
}

void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
{
    char *data = (char *)buffer->get_plane(0);
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
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)),
                            std::istreambuf_iterator<char>());
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
        fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size)
        {
            media_lib->encoders[s.id]->add_buffer(buffer);
        };
    }
    media_lib->frontend->subscribe(fe_callbacks);

    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        media_lib->encoders[streamId]->subscribe(
            [media_lib, streamId](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                write_encoded_data(buffer, size, media_lib->output_files[streamId]);
                buffer->decrease_ref_count();
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
        encoder_config_t encoder_config = entry.second->get_config();
        std::cout << "Encoder " << enc_i << " current bitrate: " << encoder_config.rate_control.bitrate.target_bitrate << " Setting to "  << new_bitrate << std::endl;
        encoder_config.rate_control.bitrate.target_bitrate = new_bitrate;
        if (entry.second->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to configure Encoder " << enc_i << std::endl;
            return 1;
        }
        enc_i++;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    std::shared_ptr<MediaLibrary> media_lib = std::make_shared<MediaLibrary>();

    // Create and configure frontend
    std::string frontend_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create(FRONTEND_SRC_ELEMENT_V4L2SRC, frontend_config_string);
    if (!frontend_expected.has_value())
    {
        std::cout << "Failed to create frontend" << std::endl;
        return 1;
    }
    media_lib->frontend = frontend_expected.value();

    auto streams = media_lib->frontend->get_outputs_streams();
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
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(encoderosd_config_string, s.id);
        if (!encoder_expected.has_value())
        {
            std::cout << "Failed to create encoder osd" << std::endl;
            return 1;
        }
        media_lib->encoders[s.id] = encoder_expected.value();

        // create and configure output file
        std::string output_file_path = OUTPUT_FILE(s.id);
        delete_output_file(output_file_path);
        media_lib->output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        if (!media_lib->output_files[s.id].good())
        {
            std::cout << "Error occurred at writing time!" << std::endl;
            return 1;
        }
    }
    subscribe_elements(media_lib);

    std::cout << "Starting frontend." << std::endl;
    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        encoder->start();
    }
    media_lib->frontend->start();
    PrivacyMaskBlenderPtr privacy_blender = media_lib->frontend->get_privacy_mask_blender();
    add_privacy_masks(privacy_blender);

    std::cout << "Started playing for 30 seconds." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(10)); // sleep for 10 seconds
    
    // Update privacy mask
    if (update_privacy_masks(privacy_blender) != 0)
        return 1;

    if (update_encoders_bitrate(media_lib->encoders) != 0)
        return 1;

    std::this_thread::sleep_for(std::chrono::seconds(20)); // sleep for 20 seconds

    std::cout << "Stopping." << std::endl;
    media_lib->frontend->stop();
    for (const auto &entry : media_lib->encoders)
    {
        entry.second->stop();
    }

    // close all file in media_lib->output_files
    for (auto &entry : media_lib->output_files)
    {
        entry.second.close();
    }

    return 0;
}
