#include <stddef.h>
#include <media_library/media_library.hpp>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <tl/expected.hpp>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <compare>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <ratio>
#include <stdexcept>
#include <string>

// Media-Library includes
#include "media_library/buffer_pool.hpp"
// Infra includes
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing_perfetto.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage_from_file.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing.hpp"
#include "hailo_analytics/pipeline/sources/file_reader_module.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"

namespace hailo_analytics::pipeline::sources
{

FrontendStageFromFile::FrontendStageFromFile(std::string name, const std::string &file_location, size_t width,
                                             size_t height, double fps, bool loop_enabled, size_t queue_size,
                                             bool leaky, size_t buffer_pool_size, StagePoolMode pool_mode,
                                             bool trace_processing_operations)
    : FrontendStage(name, queue_size, leaky, trace_processing_operations), m_feeding_thread_active(false),
      m_file_location(file_location), m_width(width), m_height(height), m_fps(fps), m_loop_enabled(loop_enabled),
      m_buffer_pool_size(buffer_pool_size), m_pool_mode(pool_mode)
{
    m_file_reader = nullptr;
}

AppStatus FrontendStageFromFile::create(MediaLibraryPtr media_library)
{
    m_file_reader = std::make_shared<FileReader>(m_stage_name + "_reader", m_file_location, m_width, m_height, m_fps,
                                                 m_loop_enabled);

    return FrontendStage::create(media_library);
}

AppStatus FrontendStageFromFile::stop()
{
    m_feeding_thread_active = false;
    m_available_buffers_cv.notify_all();
    if (m_feeding_thread.joinable())
    {
        m_feeding_thread.join();
    }

    return FrontendStage::stop();
}

AppStatus FrontendStageFromFile::init()
{
    if (m_file_reader == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("FileReader not configured for frontend {}", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    AppStatus status = m_file_reader->init();
    if (status != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to initialize file reader for frontend {}", m_stage_name);
        return status;
    }

    status = FrontendStage::init();
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
        HAILO_ANALYTICS_LOG_ERROR("Failed to initialize buffer pool for frontend stage {}", m_stage_name);
        return AppStatus::BUFFER_ALLOCATION_ERROR;
    }

    HAILO_ANALYTICS_LOG_INFO("Created buffer pool for frontend stage '{}': {} buffers of size {}x{}", m_stage_name,
                             m_buffer_pool_size, m_width, m_height);

    setup_pool_notifications();

    m_feeding_thread_active = true;
    m_feeding_thread = std::thread(&FrontendStageFromFile::feeding_thread_func, this);

    return AppStatus::SUCCESS;
}

AppStatus FrontendStageFromFile::deinit()
{
    m_feeding_thread_active = false;
    m_available_buffers_cv.notify_all();
    if (m_feeding_thread.joinable())
    {
        m_feeding_thread.join();
    }

    return FrontendStage::deinit();
}

AppStatus FrontendStageFromFile::configure(MediaLibraryPtr media_library)
{
    auto current_profile = media_library->get_current_profile();
    if (!current_profile.has_value() ||
        current_profile.value().sensor_config.input_video.source_type != frontend_src_element_t::APPSRC)
    {
        HAILO_ANALYTICS_LOG_ERROR("FrontendStageFromFile requires frontend with APPSRC source type. "
                                  "Reconfigure the frontend before calling configure().");
        return AppStatus::INVALID_ARGUMENT;
    }

    m_file_reader = std::make_shared<FileReader>(m_stage_name + "_reader", m_file_location, m_width, m_height, m_fps,
                                                 m_loop_enabled);

    return FrontendStage::configure(media_library);
}

void FrontendStageFromFile::setup_pool_notifications()
{
    if (m_pool_mode == StagePoolMode::BLOCKING)
    {
        m_buffer_pool->set_on_release_callback([this](void *) { m_available_buffers_cv.notify_all(); });
    }
}

void FrontendStageFromFile::feeding_thread_func()
{
    HAILO_ANALYTICS_LOG_INFO("Frontend feeding thread started");

    auto last_frame_time = std::chrono::steady_clock::now();
    auto frame_interval = m_file_reader->get_frame_interval();

    while (m_feeding_thread_active && !m_end_of_stream)
    {
        trace_processing_start();

        HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();

        // Check buffer availability before acquiring
        if (m_buffer_pool->get_available_buffers_count() == 0)
        {
            if (m_pool_mode == StagePoolMode::BLOCKING)
            {
                HAILO_ANALYTICS_LOG_INFO("{} no available buffers in pool, waiting...", m_stage_name);
                std::unique_lock<std::mutex> lock(m_buff_pool_mutex);
                m_available_buffers_cv.wait(lock, [this]() {
                    return m_buffer_pool->get_available_buffers_count() >= 1 || !m_feeding_thread_active;
                });
                if (!m_feeding_thread_active)
                {
                    trace_processing_end();
                    break;
                }
            }
            else
            {
                HAILO_ANALYTICS_LOG_WARN("{} no available buffers in pool, skipping frame", m_stage_name);
                trace_processing_end();
                continue;
            }
        }

        // Acquire buffer from pool - buffer pool is guaranteed to exist
        if (m_buffer_pool->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_WARN("Failed to acquire buffer from pool, skipping frame");
            trace_processing_end();
            continue;
        }

        if (!m_file_reader->read_next_frame(buffer))
        {
            HAILO_ANALYTICS_LOG_INFO("File reading completed, stopping feeding thread");
            set_end_of_stream(true);
            trace_processing_end();
            break;
        }

        buffer->isp_timestamp_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        if (m_media_library->add_buffer_to_frontend(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_WARN("Failed to add buffer to frontend, skipping frame");
            trace_processing_end();
            continue;
        }

        trace_processing_end();

        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);

        if (elapsed < frame_interval)
        {
            std::this_thread::sleep_for(frame_interval - elapsed);
        }

        last_frame_time = std::chrono::steady_clock::now();
    }

    HAILO_ANALYTICS_LOG_INFO("Frontend feeding thread ended");
}

void FrontendStageFromFile::trace_processing_start(HailoMediaLibraryBufferPtr buffer)
{
    if (m_trace_processing_operations)
    {
        if (buffer)
        {
            m_tracing->trace_processing_start(nullptr, "isp_timestamp_ms", buffer->isp_timestamp_ns / 1000000);
        }
        else
        {
            m_tracing->trace_processing_start();
        }
    }
}

void FrontendStageFromFile::trace_processing_end(HailoMediaLibraryBufferPtr /*buffer*/)
{
    if (m_trace_processing_operations)
    {
        m_tracing->trace_processing_end();
    }
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_file_location(std::string file_location)
{
    m_file_location = file_location;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_width(size_t width)
{
    m_width = width;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_height(size_t height)
{
    m_height = height;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_fps(double fps)
{
    m_fps = fps;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_loop_enabled_opt(bool loop_enabled)
{
    m_loop_enabled = loop_enabled;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_buffer_pool_size(size_t size)
{
    m_buffer_pool_size = size;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_pool_mode_opt(StagePoolMode mode)
{
    m_pool_mode = mode;
    return *this;
}

FrontendStageFromFileBuild::Builder &FrontendStageFromFileBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<FrontendStageFromFile> FrontendStageFromFileBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_file_location.has_value(), "set_file_location");
    THROW_IF_MISSING(m_width.has_value(), "set_width");
    THROW_IF_MISSING(m_height.has_value(), "set_height");
    THROW_IF_MISSING(m_fps.has_value(), "set_fps");
    THROW_IF_MISSING(m_buffer_pool_size.has_value(), "set_buffer_pool_size");

    if (m_buffer_pool_size.value() == 0)
    {
        throw std::invalid_argument("Buffer pool size must be greater than 0 for FrontendStageFromFile. Use "
                                    "set_buffer_pool_size() to set a valid size.");
    }

    return std::make_shared<FrontendStageFromFile>(m_stage_name.value(), m_file_location.value(), m_width.value(),
                                                   m_height.value(), m_fps.value(), m_loop_enabled, m_queue_size,
                                                   m_leaky, m_buffer_pool_size.value(), m_pool_mode, m_trace);
}

FrontendStageFromFileBuild::Builder FrontendStageFromFileBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sources
