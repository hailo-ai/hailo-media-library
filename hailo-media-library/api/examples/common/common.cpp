#include "common/common.hpp"

#include <signal.h>
#include <stdlib.h>
#include <tl/expected.hpp>
#include <iostream>
#include <iterator>

#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"

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

void connect_frontend_to_encoders(MediaLibrary &media_lib)
{
    auto streams_exp = media_lib.get_frontend_output_streams();
    if (!streams_exp.has_value())
    {
        std::cerr << "Failed to get output streams" << std::endl;
        return;
    }

    FrontendCallbacksMap fe_callbacks;
    for (const auto &stream : streams_exp.value())
    {
        fe_callbacks[stream.id] = [&media_lib, stream_id = stream.id](HailoMediaLibraryBufferPtr buffer, size_t) {
            media_lib.add_buffer_to_encoder(stream_id, buffer);
        };
    }
    media_lib.subscribe_to_frontend_output(fe_callbacks);
}

void subscribe_to_encoded_output(MediaLibrary &media_lib, std::map<output_stream_id_t, std::ofstream> &output_files,
                                 const std::string &output_prefix)
{
    auto streams_exp = media_lib.get_frontend_output_streams();
    if (!streams_exp.has_value())
    {
        std::cerr << "Failed to get output streams" << std::endl;
        return;
    }

    for (const auto &stream : streams_exp.value())
    {

        std::string output_path = "/home/root/" + output_prefix + "_" + stream.id + ".h264";
        output_files[stream.id].open(output_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output_files[stream.id].good())
        {
            std::cerr << "Failed to open output file: " << output_path << std::endl;
            continue;
        }

        media_lib.subscribe_to_encoder_output(
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

    connect_frontend_to_encoders(*resources.media_lib);
    subscribe_to_encoded_output(*resources.media_lib, resources.output_files, output_prefix);

    scale_osd_to_output_resolution(resources.media_lib);

    ret = resources.media_lib->start_pipeline();
    if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to start pipeline" << std::endl;
        return false;
    }

    return true;
}

void scale_osd_pixel_fields(config_stream_osd_t &osd, double scale)
{
    auto scale_int = [scale](int v) { return std::max(1, static_cast<int>(std::round(v * scale))); };
    auto scale_float = [scale](float v) { return v * static_cast<float>(scale); };

    auto scale_text_like = [&](auto &overlay_ptr) {
        if (!overlay_ptr)
            return;
        overlay_ptr->font_size = scale_int(overlay_ptr->font_size);
        if (overlay_ptr->line_thickness.has_value())
            overlay_ptr->line_thickness = scale_int(*overlay_ptr->line_thickness);
        if (overlay_ptr->outline_size.has_value())
            overlay_ptr->outline_size = scale_int(*overlay_ptr->outline_size);
        if (overlay_ptr->shadow_offset_x.has_value())
            overlay_ptr->shadow_offset_x = scale_float(*overlay_ptr->shadow_offset_x);
        if (overlay_ptr->shadow_offset_y.has_value())
            overlay_ptr->shadow_offset_y = scale_float(*overlay_ptr->shadow_offset_y);
    };

    for (auto &overlay : osd.text_overlays)
        scale_text_like(overlay);
    for (auto &overlay : osd.datetime_overlays)
        scale_text_like(overlay);
    // Image overlays use relative width/height in [0,1], so they auto-scale.
}

media_library_return scale_osd_to_output_resolution(MediaLibraryPtr media_lib)
{
    if (!media_lib)
        return media_library_return::MEDIA_LIBRARY_ERROR;

    auto profile_exp = media_lib->get_current_profile();
    if (!profile_exp.has_value())
        return media_library_return::MEDIA_LIBRARY_ERROR;
    config_profile_t profile = profile_exp.value();

    const uint32_t sensor_output_h = profile.sensor_config.input_video.resolution.height;
    if (sensor_output_h == 0)
        return media_library_return::MEDIA_LIBRARY_ERROR;

    bool any_scaled = false;
    for (auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
    {
        uint32_t encoder_input_h = 0;
        if (std::holds_alternative<hailo_encoder_config_t>(encoded_stream.encoding))
            encoder_input_h = std::get<hailo_encoder_config_t>(encoded_stream.encoding).input_stream.height;
        else if (std::holds_alternative<jpeg_encoder_config_t>(encoded_stream.encoding))
            encoder_input_h = std::get<jpeg_encoder_config_t>(encoded_stream.encoding).input_stream.height;

        if (encoder_input_h == 0 || encoder_input_h == sensor_output_h)
            continue;

        const double scale = static_cast<double>(encoder_input_h) / static_cast<double>(sensor_output_h);
        scale_osd_pixel_fields(encoded_stream.osd, scale);
        any_scaled = true;
    }

    if (!any_scaled)
        return media_library_return::MEDIA_LIBRARY_SUCCESS;
    return media_lib->set_override_parameters(profile);
}

} // namespace examples
