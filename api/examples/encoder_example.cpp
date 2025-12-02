#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/utils.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <mutex>

#define ENABLE_FILE_WRITE (1)
#define ENCODE_RESTART_LOOP_TEST (3)
#define FRONTEND_CONFIG_FILE "/usr/bin/frontend_config_example.json"
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(id)
#define OUTPUT_FILE(id) get_output_file(id)

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
    std::atomic<bool> FrontendRestarting = false;
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
#if ENABLE_FILE_WRITE
    char *data = (char *)buffer->get_plane_ptr(0);
    if (!data)
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    output_file.write(data, size);
#endif
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
        fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t) {
            if (media_lib->FrontendRestarting == false)
                media_lib->encoders[s.id]->add_buffer(buffer);
        };
    }
    media_lib->frontend->subscribe(fe_callbacks);
    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "Subscribing to encoder for '" << streamId << "'" << std::endl;
        media_lib->encoders[streamId]->subscribe([streamId, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size) {
            write_encoded_data(buffer, size, media_lib->output_files[streamId]);
        });
    }
}

int update_encoders_bitrate(std::map<output_stream_id_t, MediaLibraryEncoderPtr> &encoders)
{
    std::cout << "Updating encoder bitrate" << std::endl;
    uint32_t new_bitrate = 25000000;
    uint enc_i = 0;
    for (const auto &entry : encoders)
    {
        encoder_config_t encoder_config = entry.second->get_user_config();
        if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
        {
            continue;
        }

        hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        std::cout << "Encoder " << enc_i
                  << " current bitrate: " << hailo_encoder_config.rate_control.bitrate.target_bitrate << " Setting to "
                  << new_bitrate << std::endl;
        hailo_encoder_config.rate_control.bitrate.target_bitrate = new_bitrate;
        if (entry.second->set_config(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to configure Encoder " << enc_i << std::endl;
            return 1;
        }
        enc_i++;
    }

    return 0;
}

int force_keyframe(std::map<output_stream_id_t, MediaLibraryEncoderPtr> &encoders)
{
    std::cout << "Calling Force Keyframe on Encoders" << std::endl;

    for (const auto &entry : encoders)
    {
        if (entry.second->force_keyframe() != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to force keyframe on Encoder" << std::endl;
            return 1;
        }
    }

    return 0;
}

int main()
{
    std::shared_ptr<MediaLibrary> media_lib = std::make_shared<MediaLibrary>();

    // Create and configure frontend
    std::string preproc_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
    if (!frontend_expected.has_value())
    {
        std::cout << "Failed to create Frontend" << std::endl;
        return 1;
    }
    media_lib->frontend = frontend_expected.value();

    if (media_lib->frontend->set_config(preproc_config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to configure frontend" << std::endl;
        return 1;
    }

    auto streams = media_lib->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    for (auto s : streams.value())
    {
        // Create and configure encoder
        std::string encoderosd_config_string = read_string_from_file(ENCODER_OSD_CONFIG_FILE(s.id).c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(s.id);
        if (!encoder_expected.has_value())
        {
            std::cout << "Failed to create Encoder" << std::endl;
            return 1;
        }
        media_lib->encoders[s.id] = encoder_expected.value();
        if (media_lib->encoders[s.id]->set_config(encoderosd_config_string) != MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to configure Encoder" << std::endl;
            return 1;
        }

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

    std::cout << "Starting frontend" << std::endl;

    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        encoder->start();
    }

    media_lib->frontend->start();

    std::this_thread::sleep_for(std::chrono::seconds(3));

    for (int i = 0; i < ENCODE_RESTART_LOOP_TEST; i++)
    {
        media_lib->FrontendRestarting = true;
        output_stream_id_t streamId = "sink0";
        std::cout << "Stopping Encoder " << streamId << std::endl;
        media_lib->encoders[streamId]->stop();

        std::this_thread::sleep_for(std::chrono::seconds(3));

        std::cout << "Starting Encoder " << streamId << std::endl;
        media_lib->encoders[streamId]->start();
        media_lib->FrontendRestarting = false;

        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (force_keyframe(media_lib->encoders) != 0)
        {
            goto cleanup_exit;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (update_encoders_bitrate(media_lib->encoders) != 0)
        {
            goto cleanup_exit;
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

cleanup_exit:
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

    media_lib->frontend = nullptr;
    media_lib->encoders.clear();
    media_lib->output_files.clear();

    return 0;
}
