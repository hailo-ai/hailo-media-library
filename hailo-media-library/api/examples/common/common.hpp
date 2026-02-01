#pragma once

#include "media_library/media_library.hpp"
#include "media_library/signal_utils.hpp"
#include <fstream>
#include <string>
#include <map>

namespace examples
{

struct Resources
{
    MediaLibraryPtr media_lib;
    std::map<output_stream_id_t, std::ofstream> output_files;

    void cleanup();
};

void setup_signal_handler(Resources *resources);
std::string read_file_to_string(const std::string &filepath);
void connect_frontend_to_encoders(MediaLibraryPtr media_lib);
void subscribe_to_encoded_output(MediaLibraryPtr media_lib, std::map<output_stream_id_t, std::ofstream> &output_files,
                                 const std::string &output_prefix);
bool initialize_pipeline(Resources &resources, const std::string &config_path, const std::string &output_prefix);

} // namespace examples
