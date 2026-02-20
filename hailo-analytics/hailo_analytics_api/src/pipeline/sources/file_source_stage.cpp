#include <chrono>
#include <thread>
#include <memory>

// Media-Library includes
#include "media_library/buffer_pool.hpp"

// Infra includes
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/sources/file_source_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::sources
{

FileSourceStage::FileSourceStage(std::string name, const std::string &file_location, size_t width, size_t height,
                                 double fps, bool loop_enabled, size_t queue_size, bool leaky, size_t buffer_pool_size,
                                 bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations),
      m_file_location(file_location), m_width(width), m_height(height), m_fps(fps), m_loop_enabled(loop_enabled),
      m_buffer_pool_size(buffer_pool_size)
{
    m_file_reader =
        std::make_shared<FileReader>(m_stage_name + "_reader", file_location, width, height, fps, loop_enabled);
}

AppStatus FileSourceStage::init()
{
    if (m_file_reader == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("FileSourceStage not properly configured");
        return AppStatus::UNINITIALIZED;
    }

    AppStatus status = m_file_reader->init();
    if (status != AppStatus::SUCCESS)
    {
        return status;
    }

    // Create buffer pool - buffer_pool_size is guaranteed to be > 0 by builder validation
    const std::string pool_name = m_stage_name + "_buffer_pool";
    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(m_width, m_height, HAILO_FORMAT_NV12, m_buffer_pool_size,
                                                             HAILO_MEMORY_TYPE_DMABUF, pool_name);

    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to initialize buffer pool for file source stage {}", m_stage_name);
        return AppStatus::BUFFER_ALLOCATION_ERROR;
    }

    HAILO_ANALYTICS_LOG_INFO("Created buffer pool for file source stage '{}': {} buffers of size {}x{}", m_stage_name,
                             m_buffer_pool_size, m_width, m_height);

    return AppStatus::SUCCESS;
}

AppStatus FileSourceStage::deinit()
{
    HAILO_ANALYTICS_LOG_INFO("FileSourceStage deinitialized");
    return AppStatus::SUCCESS;
}

void FileSourceStage::loop()
{
    HAILO_ANALYTICS_LOG_INFO("FileSourceStage loop started");

    auto last_frame_time = std::chrono::steady_clock::now();
    auto frame_interval = m_file_reader->get_frame_interval();

    while (!m_end_of_stream)
    {
        trace_processing_start();

        HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();

        // Acquire buffer from pool - buffer pool is guaranteed to exist
        if (m_buffer_pool->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_WARN("Failed to acquire buffer from pool, skipping frame");
            trace_processing_end();
            continue;
        }

        if (!m_file_reader->read_next_frame(buffer))
        {
            HAILO_ANALYTICS_LOG_INFO("Stopping.");
            set_end_of_stream(true);
            trace_processing_end();
            break;
        }

        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);

        if (elapsed < frame_interval)
        {
            std::this_thread::sleep_for(frame_interval - elapsed);
        }

        BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
        wrapped_buffer->get_buffer()->isp_timestamp_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        send_to_subscribers(wrapped_buffer);

        trace_processing_end();

        last_frame_time = std::chrono::steady_clock::now();

        trace_fps();
    }

    HAILO_ANALYTICS_LOG_INFO("FileSourceStage loop ended");
}

void FileSourceStage::trace_processing_start(BufferPtr buffer)
{
    if (m_trace_processing_operations)
    {
        m_tracing->trace_processing_start(buffer);
    }
}

void FileSourceStage::trace_processing_end(BufferPtr buffer)
{
    if (m_trace_processing_operations)
    {
        m_tracing->trace_processing_end(buffer);
    }
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_file_location(std::string file_location)
{
    m_file_location = file_location;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_width(size_t width)
{
    m_width = width;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_height(size_t height)
{
    m_height = height;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_fps(double fps)
{
    m_fps = fps;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_buffer_pool_size(size_t size)
{
    m_buffer_pool_size = size;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

FileSourceStageBuild::Builder &FileSourceStageBuild::Builder::set_loop_opt(bool loop)
{
    m_loop = loop;
    return *this;
}

std::shared_ptr<FileSourceStage> FileSourceStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_file_location.has_value(), "set_file_location");
    THROW_IF_MISSING(m_width.has_value(), "set_width");
    THROW_IF_MISSING(m_height.has_value(), "set_height");
    THROW_IF_MISSING(m_fps.has_value(), "set_fps");
    THROW_IF_MISSING(m_buffer_pool_size.has_value(), "set_buffer_pool_size");

    if (m_buffer_pool_size.value() == 0)
    {
        throw std::invalid_argument("Buffer pool size must be greater than 0 for FileSourceStage. Use "
                                    "set_buffer_pool_size() to set a valid size.");
    }

    auto stage = std::make_shared<FileSourceStage>(m_stage_name.value(), m_file_location.value(), m_width.value(),
                                                   m_height.value(), m_fps.value(), m_loop, m_queue_size, false,
                                                   m_buffer_pool_size.value(), m_trace);
    return stage;
}

FileSourceStageBuild::Builder FileSourceStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sources
