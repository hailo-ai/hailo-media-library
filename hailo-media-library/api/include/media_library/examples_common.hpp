/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/**
 * @file example_common.hpp
 * @brief MediaLibrary Frontend Example Common Helper Definitions
 **/
#pragma once

#include "media_library/media_library.hpp"
#include "media_library/utils.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include <fstream>
#include <optional>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <signal.h>
#include <map>
#include <thread>
#include <cstring>

#define SWITCH_ENCODER_EXAMPLE false
#ifdef USE_SWITCH_ENCODER_EXAMPLE
#undef SWITCH_ENCODER_EXAMPLE
#define SWITCH_ENCODER_EXAMPLE true
#endif

#define JPEG_SINK1 false
#ifdef USE_JPEG_JSONS
#undef JPEG_SINK1
#define JPEG_SINK1 true
#define IS_JPEG(id) (id != "sink0")
#define FILE_ID(id) (IS_JPEG(id) ? "jpeg_" + id : id)
#else
#define FILE_ID(id) (id)
#define IS_JPEG(id) (false)
#endif

#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(FILE_ID(id))
#define OUTPUT_FILE(id) get_output_file(FILE_ID(id), IS_JPEG(id))

inline MediaLibraryPtr m_media_lib;
inline std::map<output_stream_id_t, std::ofstream> m_output_files;
inline std::optional<config_profile_t> m_user_profile;

inline std::string get_encoder_osd_config_file(const std::string &id)
{
    return "/usr/bin/frontend_encoder_" + id + ".json";
}

inline std::string get_output_file(const std::string &id, bool is_jpeg)
{
    std::string suffix = (is_jpeg ? ".jpegenc" : ".h264");
    return "/var/volatile/tmp/frontend_example_" + id + suffix;
}

inline void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
{
    char *data = (char *)buffer->get_plane_ptr(0);
    if (!data)
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    output_file.write(data, size);
}

inline void delete_output_file(std::string output_file)
{
    std::ofstream fp(output_file.c_str(), std::ios::out | std::ios::binary);
    if (!fp.good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    fp.close();
}

inline void subscribe_elements(MediaLibraryPtr media_lib)
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

inline void cleanup_resources()
{
    sleep(2);
    if (m_media_lib)
    {
        m_media_lib->m_frontend = nullptr;
        m_media_lib->m_encoders.clear();
    }

    // close all file in m_media_lib->output_files
    for (auto &entry : m_output_files)
    {
        entry.second.close();
    }

    m_output_files.clear();
    m_media_lib = nullptr;
}
