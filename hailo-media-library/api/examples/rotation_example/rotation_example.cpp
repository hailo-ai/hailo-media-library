#include <stdint.h>
#include <stdlib.h>
#include <tl/expected.hpp>
#include <map>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "common/common.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/encoder_config_types.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library.hpp"
#include "media_library/utils.hpp"
#include "cloexec_fstream.hpp"

// Track current rotation state
static rotation_angle_t g_current_rotation = ROTATION_ANGLE_0;

// Store original OSD positions from the initial profile (before any rotation)
struct overlay_original_pos_t
{
    float x;
    float y;
    float width; // for image overlays
};
// Map: stream_id -> overlay_id -> original position
static std::map<std::string, std::map<std::string, overlay_original_pos_t>> g_original_osd_positions;

int rotation_angle_to_degrees(rotation_angle_t angle)
{
    switch (angle)
    {
    case ROTATION_ANGLE_0:
        return 0;
    case ROTATION_ANGLE_90:
        return 90;
    case ROTATION_ANGLE_180:
        return 180;
    case ROTATION_ANGLE_270:
        return 270;
    default:
        return 0;
    }
}

bool is_rotated_90_or_270(rotation_angle_t angle)
{
    return (angle == ROTATION_ANGLE_90 || angle == ROTATION_ANGLE_270);
}

void update_osd_rotation_policy_in_profile(config_profile_t &profile)
{
    for (auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
    {
        // Update text overlays
        for (auto &text_overlay : encoded_stream.osd.text_overlays)
        {
            text_overlay->rotation_alignment_policy = rotation_alignment_policy_t::CENTER;
        }

        // Update datetime overlays
        for (auto &datetime_overlay : encoded_stream.osd.datetime_overlays)
        {
            datetime_overlay->rotation_alignment_policy = rotation_alignment_policy_t::CENTER;
        }

        // Update image overlays
        for (auto &image_overlay : encoded_stream.osd.image_overlays)
        {
            image_overlay->rotation_alignment_policy = rotation_alignment_policy_t::CENTER;
        }
    }
}

// Save original OSD positions for a stream (called once before first rotation)
void save_original_osd_positions(const std::string &stream_id, const config_stream_osd_t &osd)
{
    auto &stream_positions = g_original_osd_positions[stream_id];

    for (const auto &text_overlay : osd.text_overlays)
    {
        stream_positions[text_overlay->id] = {text_overlay->x, text_overlay->y, 0.0f};
    }
    for (const auto &datetime_overlay : osd.datetime_overlays)
    {
        stream_positions[datetime_overlay->id] = {datetime_overlay->x, datetime_overlay->y, 0.0f};
    }
    for (const auto &image_overlay : osd.image_overlays)
    {
        stream_positions[image_overlay->id] = {image_overlay->x, image_overlay->y, image_overlay->width};
    }
}

// Restore original OSD positions from saved state before applying new adjustments
void restore_original_osd_positions(const std::string &stream_id, config_stream_osd_t &osd)
{
    auto stream_it = g_original_osd_positions.find(stream_id);
    if (stream_it == g_original_osd_positions.end())
        return;

    const auto &stream_positions = stream_it->second;

    for (auto &text_overlay : osd.text_overlays)
    {
        auto it = stream_positions.find(text_overlay->id);
        if (it != stream_positions.end())
        {
            text_overlay->x = it->second.x;
            text_overlay->y = it->second.y;
        }
    }
    for (auto &datetime_overlay : osd.datetime_overlays)
    {
        auto it = stream_positions.find(datetime_overlay->id);
        if (it != stream_positions.end())
        {
            datetime_overlay->x = it->second.x;
            datetime_overlay->y = it->second.y;
        }
    }
    for (auto &image_overlay : osd.image_overlays)
    {
        auto it = stream_positions.find(image_overlay->id);
        if (it != stream_positions.end())
        {
            image_overlay->x = it->second.x;
            image_overlay->y = it->second.y;
        }
    }
}

// Adjust OSD positions to fit within new aspect ratio when dimensions are swapped
void adjust_osd_positions_for_aspect_ratio_swap(const std::string &stream_id, config_stream_osd_t &osd,
                                                float old_aspect, float new_aspect)
{
    (void)old_aspect;

    // First restore original positions so we always adjust from the baseline
    restore_original_osd_positions(stream_id, osd);

    // In portrait mode (aspect < 1), clamp x positions to fit the narrower frame
    if (new_aspect >= 1.0f)
        return; // Landscape — original positions are fine as-is

    float x_scale = new_aspect;

    for (auto &text_overlay : osd.text_overlays)
    {
        float new_x = text_overlay->x * x_scale;
        if (text_overlay->x != new_x)
        {
            std::cout << "  Adjusting text overlay '" << text_overlay->id << "' x: " << text_overlay->x << " -> "
                      << new_x << std::endl;
            text_overlay->x = new_x;
        }
    }

    for (auto &datetime_overlay : osd.datetime_overlays)
    {
        float new_x = datetime_overlay->x * x_scale;
        if (datetime_overlay->x != new_x)
        {
            std::cout << "  Adjusting datetime overlay '" << datetime_overlay->id << "' x: " << datetime_overlay->x
                      << " -> " << new_x << std::endl;
            datetime_overlay->x = new_x;
        }
    }

    for (auto &image_overlay : osd.image_overlays)
    {
        float new_x = image_overlay->x * x_scale;
        if (image_overlay->x != new_x)
        {
            std::cout << "  Adjusting image overlay '" << image_overlay->id << "' x: " << image_overlay->x << " -> "
                      << new_x << std::endl;
            image_overlay->x = new_x;
        }
    }
}

void set_rotation_degrees(MediaLibraryPtr media_library, rotation_angle_t rotation_angle)
{
    std::cout << "\n=== Rotation override via config profile (" << rotation_angle_to_degrees(rotation_angle)
              << " degrees) ===" << std::endl;

    if (!media_library)
    {
        std::cout << "ERROR: media_library is null" << std::endl;
        return;
    }

    // Check if swap is needed based on previous and new rotation
    bool prev_is_90_270 = is_rotated_90_or_270(g_current_rotation);
    bool new_is_90_270 = is_rotated_90_or_270(rotation_angle);
    bool needs_swap = (prev_is_90_270 != new_is_90_270);

    // Get current profile
    auto original_profile_exp = media_library->get_current_profile();
    if (!original_profile_exp.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return;
    }

    config_profile_t profile = original_profile_exp.value();
    std::cout << "Current profile: " << profile.name << std::endl;

    // Enable rotation in application settings
    profile.application_settings.rotation.enabled = (rotation_angle != ROTATION_ANGLE_0);
    profile.application_settings.rotation.angle = rotation_angle;

    std::cout << "Setting rotation angle to: " << rotation_angle_to_degrees(rotation_angle) << " degrees" << std::endl;

    // Save original OSD positions on first rotation call
    if (g_original_osd_positions.empty())
    {
        for (const auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
        {
            save_original_osd_positions(stream_id, encoded_stream.osd);
        }
    }

    // Only swap width/height when transitioning between 0/180 and 90/270
    if (needs_swap)
    {
        std::cout << "Swapping encoder dimensions..." << std::endl;
        for (auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
        {
            uint32_t old_width = 0, old_height = 0;
            uint32_t *width_ptr = nullptr;
            uint32_t *height_ptr = nullptr;

            if (std::holds_alternative<hailo_encoder_config_t>(encoded_stream.encoding))
            {
                auto &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoded_stream.encoding);
                auto &input_stream = hailo_encoder_config.input_stream;
                old_width = input_stream.width;
                old_height = input_stream.height;
                width_ptr = &input_stream.width;
                height_ptr = &input_stream.height;
            }
            else if (std::holds_alternative<jpeg_encoder_config_t>(encoded_stream.encoding))
            {
                auto &jpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoded_stream.encoding);
                auto &input_stream = jpeg_encoder_config.input_stream;
                old_width = input_stream.width;
                old_height = input_stream.height;
                width_ptr = &input_stream.width;
                height_ptr = &input_stream.height;
            }

            if (width_ptr && height_ptr)
            {
                std::cout << "[" << stream_id << "] " << old_width << "x" << old_height << " -> " << old_height << "x"
                          << old_width << std::endl;
                std::swap(*width_ptr, *height_ptr);

                // Adjust OSD positions for the NEW dimensions (after swap),
                // always starting from the original saved positions
                uint32_t new_width = *width_ptr;
                uint32_t new_height = *height_ptr;
                float new_aspect = static_cast<float>(new_width) / static_cast<float>(new_height);
                float old_aspect = static_cast<float>(old_width) / static_cast<float>(old_height);
                std::cout << "[" << stream_id << "] Adjusting OSD for aspect ratio change (old: " << old_aspect
                          << " -> new: " << new_aspect << ")" << std::endl;
                adjust_osd_positions_for_aspect_ratio_swap(stream_id, encoded_stream.osd, old_aspect, new_aspect);
            }
        }
    }
    else
    {
        std::cout << "No dimension swap needed" << std::endl;
        // Even without a swap, restore original OSD positions in case they were
        // clamped by a previous rotation (e.g., 90° -> 180° keeps portrait but
        // we still want correct positions)
        for (auto &[stream_id, encoded_stream] : profile.encoded_output_streams)
        {
            restore_original_osd_positions(stream_id, encoded_stream.osd);
        }
    }

    // Update OSD rotation policy to CENTER for all overlays
    update_osd_rotation_policy_in_profile(profile);

    // Apply all changes in one call: rotation + dimensions + adjusted OSD
    std::cout << "Applying rotation, dimensions, and OSD changes..." << std::endl;
    media_library_return ret = media_library->set_override_parameters(profile);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to override parameters for rotation" << std::endl;
        return;
    }

    // Update current rotation state
    g_current_rotation = rotation_angle;

    std::cout << "Rotation to " << rotation_angle_to_degrees(rotation_angle) << " degrees applied successfully."
              << std::endl;
}

int main()
{
    static signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([](int signal) {
        if (m_media_lib)
            m_media_lib->stop_pipeline();
        cleanup_resources();
        exit(signal);
    });

    m_user_profile = std::nullopt;
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_string = read_string_from_file(get_config_path().c_str());

    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }

    examples::scale_osd_to_output_resolution(m_media_lib);

    auto streams = m_media_lib->m_frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return 1;
    }
    for (auto s : streams.value())
    {
        std::string output_file_path = OUTPUT_FILE_WITH_PREFIX("rotation", s.id);
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

    std::cout << "Playing in default orientation for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // --- Test Rotation Cycle ---
    std::cout << "\n>>> Rotating to 90 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_90);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "\n>>> Rotating to 180 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_180);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "\n>>> Rotating to 270 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_270);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "\n>>> Rotating to 0 degrees <<<" << std::endl;
    set_rotation_degrees(m_media_lib, ROTATION_ANGLE_0);
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "Stopping pipeline." << std::endl;
    m_media_lib->stop_pipeline();
    cleanup_resources();

    return 0;
}
