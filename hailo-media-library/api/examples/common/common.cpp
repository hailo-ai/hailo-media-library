#include "common/common.hpp"
#include <iostream>

namespace examples
{

void Resources::cleanup()
{
    if (media_lib)
    {
        media_lib->stop_pipeline();
        media_lib = nullptr;
    }
    for (auto &[id, file] : output_files)
    {
        file.close();
    }
    output_files.clear();
}

void setup_signal_handler(Resources *resources)
{
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([resources](int) {
        resources->cleanup();
        exit(0);
    });
}

std::string read_file_to_string(const std::string &filepath)
{
    std::ifstream file(filepath);
    if (!file.good())
    {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void connect_frontend_to_encoders(MediaLibraryPtr media_lib)
{
    auto streams_exp = media_lib->m_frontend->get_outputs_streams();
    if (!streams_exp.has_value())
    {
        std::cerr << "Failed to get output streams" << std::endl;
        return;
    }

    FrontendCallbacksMap fe_callbacks;
    for (const auto &stream : streams_exp.value())
    {
        fe_callbacks[stream.id] = [media_lib, stream_id = stream.id](HailoMediaLibraryBufferPtr buffer, size_t) {
            if (media_lib->m_encoders.find(stream_id) != media_lib->m_encoders.end())
            {
                media_lib->m_encoders[stream_id]->add_buffer(buffer);
            }
        };
    }
    media_lib->subscribe_to_frontend_output(fe_callbacks);
}

void subscribe_to_encoded_output(MediaLibraryPtr media_lib, std::map<output_stream_id_t, std::ofstream> &output_files,
                                 const std::string &output_prefix)
{
    auto streams_exp = media_lib->m_frontend->get_outputs_streams();
    if (!streams_exp.has_value())
    {
        std::cerr << "Failed to get output streams" << std::endl;
        return;
    }

    for (const auto &stream : streams_exp.value())
    {
        if (media_lib->m_encoders.find(stream.id) == media_lib->m_encoders.end())
        {
            continue;
        }

        std::string output_path = "/home/root/" + output_prefix + "_" + stream.id + ".h264";
        output_files[stream.id].open(output_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output_files[stream.id].good())
        {
            std::cerr << "Failed to open output file: " << output_path << std::endl;
            continue;
        }

        media_lib->subscribe_to_encoder_output(
            stream.id, [&output_files, stream_id = stream.id](HailoMediaLibraryBufferPtr buffer, size_t size) {
                char *data = static_cast<char *>(buffer->get_plane_ptr(0));
                if (data)
                {
                    output_files[stream_id].write(data, size);
                }
            });
    }
}

bool initialize_pipeline(Resources &resources, const std::string &config_path, const std::string &output_prefix)
{
    auto media_lib_exp = MediaLibrary::create();
    if (!media_lib_exp.has_value())
    {
        std::cerr << "Failed to create MediaLibrary" << std::endl;
        return false;
    }
    resources.media_lib = media_lib_exp.value();

    std::string config_string = read_file_to_string(config_path);
    auto ret = resources.media_lib->initialize(config_string);
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to initialize MediaLibrary" << std::endl;
        return false;
    }

    connect_frontend_to_encoders(resources.media_lib);
    subscribe_to_encoded_output(resources.media_lib, resources.output_files, output_prefix);

    ret = resources.media_lib->start_pipeline();
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to start pipeline" << std::endl;
        return false;
    }

    return true;
}

} // namespace examples
