#include <tl/expected.hpp>
#include <stdint.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>
#include <functional>
#include <map>
#include <vector>

#include "media_library/cloexec_fstream.hpp"
#include "media_library/media_library.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/media_library_api_types.hpp"
#include "buffer_pool.hpp"

static constexpr const char *OUTPUT_DIR = "/var/volatile/tmp";
static constexpr const char *OUTPUT_FILE_PREFIX = "client_encoded_output";
static constexpr const char *DEFAULT_FILE_EXTENSION = ".h264";
static constexpr uint32_t FPS_LOG_INTERVAL_FRAMES = 30;

static MediaLibraryInterfacePtr m_media_lib;
static std::map<output_stream_id_t, cloexec::ofstream> m_output_files;

static void close_output_files()
{
    for (auto &[stream_id, file] : m_output_files)
    {
        if (file.is_open())
        {
            file.close();
            std::cout << "Closed output file for stream '" << stream_id << "'" << std::endl;
        }
    }
    m_output_files.clear();
}

static void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, cloexec::ofstream &output_file)
{
    char *data = static_cast<char *>(buffer->get_plane_ptr(0));
    if (!data)
    {
        std::cerr << "Error: null data pointer when writing encoded data" << std::endl;
        return;
    }
    output_file.write(data, size);
}

static std::string build_output_path(const output_stream_id_t &stream_id)
{
    return std::string(OUTPUT_DIR) + "/" + OUTPUT_FILE_PREFIX + "_" + stream_id + DEFAULT_FILE_EXTENSION;
}

static bool setup_subscriptions()
{
    // Get available frontend output streams to discover stream IDs
    auto streams_result = m_media_lib->get_frontend_output_streams();
    if (!streams_result.has_value())
    {
        std::cerr << "Failed to get frontend output streams" << std::endl;
        return false;
    }

    const auto &streams = streams_result.value();
    std::cout << "Found " << streams.size() << " frontend output stream(s)" << std::endl;

    // 1. Subscribe to frontend output -- forward each buffer to the encoder
    FrontendCallbacksMap frontend_callbacks;
    for (const auto &stream : streams)
    {
        std::cout << "Registering frontend callback for stream '" << stream.id << "' (" << stream.width << "x"
                  << stream.height << ")" << std::endl;

        frontend_callbacks[stream.id] = [stream_id = stream.id, frame_count = uint32_t(0), has_matching_encoder = true](
                                            HailoMediaLibraryBufferPtr buffer, size_t) mutable {
            if (frame_count % FPS_LOG_INTERVAL_FRAMES == 0)
            {
                auto streams = m_media_lib->get_frontend_output_streams();
                if (streams.has_value())
                {
                    for (const auto &s : streams.value())
                    {
                        if (s.id == stream_id)
                        {
                            std::cout << "[Frontend] stream '" << stream_id << "' current_fps=" << s.current_fps
                                      << std::endl;
                            break;
                        }
                    }
                }
                frame_count = 0;
            }
            frame_count++;

            if (has_matching_encoder)
            {
                media_library_return ret = m_media_lib->add_buffer_to_encoder(stream_id, buffer);
                if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
                {
                    std::cerr << "Stream '" << stream_id << "' has no matching encoder, skipping" << std::endl;
                    has_matching_encoder = false;
                }
            }
        };
    }

    media_library_return ret = m_media_lib->subscribe_to_frontend_output(frontend_callbacks);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to subscribe to frontend output" << std::endl;
        return false;
    }
    std::cout << "Subscribed to frontend output" << std::endl;

    // 2. Subscribe to encoder output -- save encoded data to local files
    for (const auto &stream : streams)
    {
        std::string output_path = build_output_path(stream.id);
        m_output_files[stream.id].open(output_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!m_output_files[stream.id].good())
        {
            std::cerr << "Failed to open output file: " << output_path << std::endl;
            continue;
        }
        std::cout << "Opened output file: " << output_path << std::endl;

        ret = m_media_lib->subscribe_to_encoder_output(
            stream.id,
            [stream_id = stream.id, frame_count = uint32_t(0),
             last_log_time = std::chrono::steady_clock::now()](HailoMediaLibraryBufferPtr buffer, size_t size) mutable {
                frame_count++;
                if (frame_count % FPS_LOG_INTERVAL_FRAMES == 0)
                {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed_sec = std::chrono::duration<float>(now - last_log_time).count();
                    if (elapsed_sec > 0.0f)
                    {
                        float fps = static_cast<float>(FPS_LOG_INTERVAL_FRAMES) / elapsed_sec;
                        std::cout << "[Encoder] stream '" << stream_id << "' fps=" << fps << std::endl;
                    }
                    last_log_time = now;
                    frame_count = 0;
                }
                write_encoded_data(buffer, static_cast<uint32_t>(size), m_output_files[stream_id]);
            });
        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to subscribe to encoder output for stream '" << stream.id << "'" << std::endl;
            continue;
        }
        std::cout << "Subscribed to encoder output for stream '" << stream.id << "'" << std::endl;
    }

    return true;
}

int main(int argc, char *argv[])
{
    std::string profile_name;
    int run_time = 90;

    int opt;
    while ((opt = getopt(argc, argv, "p:t:")) != -1)
    {
        switch (opt)
        {
        case 'p':
            profile_name = optarg;
            break;
        case 't':
            run_time = std::atoi(optarg);
            break;
        default:
            std::cerr << "Usage: " << argv[0] << " [-p profile_name] [-t seconds]" << std::endl;
            return 1;
        }
    }

    std::string medialib_config_path = "/etc/imaging/cfg/medialib_configs/face_landmarks_medialib_config.json";

    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());

    // Create MediaLibraryClient — connects to the local gRPC service (uses default address/port)
    auto media_lib_expected = media_library_service::MediaLibraryClient::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library client" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    // Initialize — delegates to gRPC service
    media_library_return init_status = m_media_lib->initialize(medialib_config_string);
    if (init_status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to initialize media library client (gRPC)" << std::endl;
        return 1;
    }
    std::cout << "Media library client initialized (gRPC)" << std::endl;

    // Subscribe to pipeline state changes
    m_media_lib->subscribe_to_pipeline_state_change([](media_library_pipeline_state_t state) {
        switch (state)
        {
        case media_library_pipeline_state_t::PIPELINE_STATE_RUNNING:
            std::cout << "[State Change] Pipeline is now RUNNING" << std::endl;
            break;
        case media_library_pipeline_state_t::PIPELINE_STATE_STOPPED:
            std::cout << "[State Change] Pipeline is now STOPPED" << std::endl;
            break;
        case media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED:
            std::cout << "[State Change] Pipeline is now UNINITIALIZED" << std::endl;
            break;
        default:
            std::cout << "[State Change] Pipeline entered unknown state: " << static_cast<int>(state) << std::endl;
            break;
        }
    });

    // Set up frontend-to-encoder and encoder-to-file subscriptions before starting the pipeline
    if (!setup_subscriptions())
    {
        std::cerr << "Failed to set up subscriptions, continuing without frontend/encoder callbacks" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Start pipeline via gRPC
    media_library_return start_status = m_media_lib->start_pipeline();
    if (start_status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to start pipeline" << std::endl;
        return 1;
    }
    std::cout << "Pipeline started" << std::endl;

    if (!profile_name.empty())
    {
        media_library_return profile_ret = m_media_lib->set_profile(profile_name);
        if (profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to set profile '" << profile_name << "'" << std::endl;
            return 1;
        }
        std::cout << "Profile set to '" << profile_name << "'" << std::endl;
    }

    // Check pipeline state after starting
    media_library_pipeline_state_t state_after_start = m_media_lib->get_pipeline_state();
    std::cout << "Pipeline state after start: " << static_cast<int>(state_after_start)
              << (state_after_start == media_library_pipeline_state_t::PIPELINE_STATE_RUNNING ? " (RUNNING)" : "")
              << std::endl;

    // Register signal handler for graceful shutdown
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        media_library_return stop_status =
            m_media_lib->shutdown(); // shutdown the client but let the service keep streaming
        if (stop_status != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to shutdown" << std::endl;
        }
        else
        {
            std::cout << "Shutdown complete" << std::endl;
        }
        m_media_lib = nullptr;
        close_output_files();
        exit(signal);
    });

    std::cout << "Running for " << run_time << " seconds" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(run_time));

    std::cout << "Stopping" << std::endl;
    media_library_return stop_status = m_media_lib->stop_pipeline(); // stop the service's stream and exit
    if (stop_status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to stop pipeline" << std::endl;
        return 1;
    }
    std::cout << "Pipeline stopped" << std::endl;

    stop_status = m_media_lib->shutdown(); // exit after stopping the stream
    if (stop_status != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to shutdown" << std::endl;
        return 1;
    }
    m_media_lib = nullptr;
    close_output_files();
    return 0;
}
