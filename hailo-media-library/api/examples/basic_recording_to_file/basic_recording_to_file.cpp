#include <stdlib.h>
#include <tl/expected.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/common.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library.hpp"
#include "media_library/media_library_api_types.hpp"
#include "media_library/utils.hpp"

int main()
{
    // register signal SIGINT and signal handler
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        if (m_media_lib)
            m_media_lib->stop_pipeline();
        cleanup_resources();
        exit(signal);
    });

    std::string current_profile_name = "";
    m_user_profile = std::nullopt;
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_path = config_path;
    if (JPEG_SINK1)
    {
        medialib_config_path = jpeg_config_path;
    }

    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());

    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }

    examples::scale_osd_to_output_resolution(m_media_lib);

    m_media_lib->subscribe_to_pipeline_state_change([](media_library_pipeline_state_t state) {
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

    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return 1;
    }
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

    std::cout << "Started playing for 30 seconds." << std::endl;

    for (int i = 0; i < 6; i++)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        for (const auto &entry : m_media_lib->m_encoders)
        {
            output_stream_id_t streamId = entry.first;
            MediaLibraryEncoderPtr encoder = entry.second;
            std::cout << "Current fps for " << streamId << " is " << encoder->get_current_fps() << std::endl;
        }
    }

    std::cout << "Stopping pipeline." << std::endl;
    if (m_media_lib->stop_pipeline() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to stop pipeline" << std::endl;
        return 1;
    }

    cleanup_resources();
    return 0;
}
