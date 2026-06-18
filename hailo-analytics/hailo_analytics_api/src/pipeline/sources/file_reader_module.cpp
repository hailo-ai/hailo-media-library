#include <stddef.h>
#include <stdint.h>
#include <media_library/buffer_pool.hpp>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include "media_library/cloexec_fstream.hpp"

// Media-Library includes
#include "media_library/dma_memory_allocator.hpp"
// Postporcess Tools includes
#include "hailo_analytics/pipeline/sources/file_reader_module.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::sources
{

FileReader::FileReader(const std::string &name, const std::string &file_location, size_t width, size_t height,
                       double fps, bool loop_enabled)
    : m_file_location(file_location), m_width(width), m_height(height), m_frame_size(0), m_total_frames(0),
      m_current_frame_index(0), m_total_frames_processed(0), m_loop_enabled(loop_enabled), m_fps(fps),
      m_frame_interval(33), m_name(name)
{
    m_frame_size = calculate_frame_size();
    m_y_plane_size = m_width * m_height;
    m_uv_plane_size = m_width * m_height / 2;
    m_frame_interval = std::chrono::milliseconds(static_cast<long>(1000.0 / fps));

    HAILO_ANALYTICS_LOG_INFO("Configured FileReader '{}' with file: {}, resolution: {}x{}, fps: {}", name,
                             file_location, width, height, fps);
}

AppStatus FileReader::init()
{
    bool is_configured = !m_file_location.empty() && m_width > 0 && m_height > 0 && m_fps > 0.0;

    if (!is_configured)
    {
        HAILO_ANALYTICS_LOG_ERROR("FileReader '{}' not properly configured", m_name);
        return AppStatus::UNINITIALIZED;
    }

    if (!validate_file())
    {
        HAILO_ANALYTICS_LOG_ERROR("File validation failed for '{}': {}", m_name, m_file_location);
        return AppStatus::CONFIGURATION_ERROR;
    }

    HAILO_ANALYTICS_LOG_INFO("File validation successful for '{}': {} frames of size {} bytes each", m_name,
                             m_total_frames, m_frame_size);

    m_file_stream.open(m_file_location, std::ios::in | std::ios::binary);
    if (!m_file_stream.is_open())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to open file for '{}': {}", m_name, m_file_location);
        return AppStatus::CONFIGURATION_ERROR;
    }

    HAILO_ANALYTICS_LOG_INFO("FileReader '{}' initialized successfully", m_name);
    return AppStatus::SUCCESS;
}

AppStatus FileReader::deinit()
{
    if (m_file_stream.is_open())
    {
        m_file_stream.close();
    }

    HAILO_ANALYTICS_LOG_INFO("FileReader '{}' deinitialized", m_name);
    return AppStatus::SUCCESS;
}

bool FileReader::read_next_frame(HailoMediaLibraryBufferPtr buffer)
{
    if (!m_file_stream.is_open())
    {
        HAILO_ANALYTICS_LOG_ERROR("File stream is not open for '{}'", m_name);
        return false;
    }

    // Handle end-of-file and loopback logic upfront
    if (m_current_frame_index >= m_total_frames)
    {
        if (m_loop_enabled)
        {
            HAILO_ANALYTICS_LOG_DEBUG("FileReader '{}' looping back to beginning of file", m_name);
            m_file_stream.clear(); // Clear EOF flag
            m_current_frame_index = 0;
        }
        else
        {
            HAILO_ANALYTICS_LOG_INFO("FileReader '{}' reached end of file", m_name);
            return false;
        }
    }

    m_file_stream.seekg(m_current_frame_index * m_frame_size, std::ios::beg);

    auto read_plane = [&](int plane_index, size_t size, const char *plane_name) -> bool {
        DmaMemoryAllocator::get_instance().dmabuf_sync_start(buffer->get_plane_ptr(plane_index));
        m_file_stream.read(reinterpret_cast<char *>(buffer->get_plane_ptr(plane_index)), size);
        DmaMemoryAllocator::get_instance().dmabuf_sync_end(buffer->get_plane_ptr(plane_index));

        if (!m_file_stream)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to read {} plane for '{}'", plane_name, m_name);
            return false;
        }

        return true;
    };

    if (!read_plane(0, m_y_plane_size, "Y"))
    {
        return false;
    }

    if (!read_plane(1, m_uv_plane_size, "UV"))
    {
        return false;
    }

    buffer->pts =
        static_cast<uint64_t>(m_total_frames_processed * m_frame_interval.count() * 1000000); // PTS in nanoseconds
    m_current_frame_index++;
    m_total_frames_processed++;

    return true;
}

void FileReader::reset()
{
    m_current_frame_index = 0;
    m_total_frames_processed = 0;
    if (m_file_stream.is_open())
    {
        m_file_stream.clear();
        m_file_stream.seekg(0, std::ios::beg);
    }
    HAILO_ANALYTICS_LOG_DEBUG("FileReader '{}' reset to beginning", m_name);
}

size_t FileReader::calculate_frame_size() const
{
    return m_width * m_height * 3 / 2;
}

bool FileReader::validate_file()
{
    cloexec::ifstream test_stream(m_file_location, std::ios::in | std::ios::binary | std::ios::ate);
    if (!test_stream.is_open())
    {
        HAILO_ANALYTICS_LOG_ERROR("Cannot open file for '{}': {}", m_name, m_file_location);
        return false;
    }

    // Get file size
    auto file_size = test_stream.tellg();
    test_stream.close();

    if (file_size <= 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("File is empty for '{}': {}", m_name, m_file_location);
        return false;
    }

    size_t expected_frame_size = calculate_frame_size();

    if (file_size % expected_frame_size != 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("File size ({}) is not a multiple of frame size ({}) for '{}'. "
                                  "File may be corrupted or have incorrect resolution.",
                                  static_cast<size_t>(file_size), expected_frame_size, m_name);
        return false;
    }

    m_total_frames = file_size / expected_frame_size;

    if (m_total_frames == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Calculated zero frames in file for '{}'", m_name);
        return false;
    }

    HAILO_ANALYTICS_LOG_INFO("File validation successful for '{}': {} frames of size {} bytes each", m_name,
                             m_total_frames, expected_frame_size);

    return true;
}

FileReader::~FileReader()
{
    deinit();
}

std::chrono::milliseconds FileReader::get_frame_interval() const
{
    return m_frame_interval;
}

size_t FileReader::get_total_frames() const
{
    return m_total_frames;
}

size_t FileReader::get_current_frame_index() const
{
    return m_current_frame_index;
}

bool FileReader::is_loop_enabled() const
{
    return m_loop_enabled;
}

double FileReader::get_fps() const
{
    return m_fps;
}

} // namespace hailo_analytics::pipeline::sources
