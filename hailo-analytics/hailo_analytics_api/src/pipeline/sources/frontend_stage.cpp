#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::sources
{

FrontendStage::FrontendStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_started(false)
{
    m_frontend = nullptr;
    m_stream_subscribers.clear();
}

FrontendStage::~FrontendStage()
{
    if (m_frontend)
    {
        m_frontend->unsubscribe_all();
    }
    m_stream_subscribers.clear();
}

AppStatus FrontendStage::create(MediaLibraryFrontend &frontend)
{
    m_frontend = &frontend;
    return subscribe_output_streams();
}

void FrontendStage::add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id)
{
    if (!stream_id.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Stream ID must be provided when subscribing to FrontendStage");
        throw std::invalid_argument("Stream ID must be provided when subscribing to FrontendStage");
    }
    auto status = subscribe_to_stream(stream_id.value(), subscriber);
    if (status != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to subscribe to stream '{}'", stream_id.value());
        throw std::runtime_error("Failed to subscribe to stream");
    }
}

// Note subscription is done by stream id as forntend has multiple output streams
AppStatus FrontendStage::subscribe_to_stream(output_stream_id_t stream_id,
                                             hailo_analytics::pipeline::StagePtr subscriber)
{
    if (!m_frontend)
    {
        HAILO_ANALYTICS_LOG_ERROR("Frontend {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    auto streams = m_frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        return AppStatus::UNINITIALIZED;
    }
    if (std::find_if(streams.value().begin(), streams.value().end(), [stream_id](const frontend_output_stream_t &s) {
            return s.id == stream_id;
        }) == streams.value().end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Stream id '{}' not found in frontend output streams", stream_id);
        return AppStatus::INVALID_ARGUMENT;
    }
    m_stream_subscribers[stream_id].push_back(subscriber);
    subscriber->add_queue(stream_id);
    return AppStatus::SUCCESS;
}

AppStatus FrontendStage::subscribe_output_streams()
{
    if (m_frontend == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("Frontend {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    // Get frontend output streams
    auto streams = m_frontend->get_outputs_streams();
    // Subscribe to frontend
    FrontendCallbacksMap fe_callbacks;
    if (!streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }
    for (auto s : streams.value())
    {
        HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for '{}'", s.id);
        fe_callbacks[s.id] = [s, this](HailoMediaLibraryBufferPtr buffer, [[maybe_unused]] size_t size) {
            // Only push buffers if the stage has been started
            if (!m_started.load())
            {
                HAILO_ANALYTICS_LOG_DEBUG("FrontendStage '{}': Dropping buffer for stream '{}' - stage not started yet",
                                          m_stage_name, s.id);
                return;
            }

            hailo_analytics::pipeline::BufferPtr wrapped_buffer =
                std::make_shared<hailo_analytics::pipeline::Buffer>(buffer);
            for (auto &subscriber : m_stream_subscribers[s.id])
            {
                subscriber->push(wrapped_buffer, s.id);
            }
        };
    }
    m_frontend->subscribe(fe_callbacks);
    return AppStatus::SUCCESS;
}

AppStatus FrontendStage::stop()
{
    set_end_of_stream(true);
    m_running_cv.notify_one();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    return deinit();
}

AppStatus FrontendStage::init()
{
    if (m_frontend == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("Frontend {} not configured. Call configure()");
        return AppStatus::UNINITIALIZED;
    }

    // Set started flag before starting the frontend
    m_started.store(true);

    auto status = m_frontend->start();
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to start frontend");
        m_started.store(false);
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    HAILO_ANALYTICS_LOG_INFO("FrontendStage '{}' started - now accepting buffers", m_stage_name);
    return AppStatus::SUCCESS;
}

AppStatus FrontendStage::deinit()
{
    // Clear started flag before stopping the frontend
    m_started.store(false);
    HAILO_ANALYTICS_LOG_INFO("FrontendStage '{}' stopping - no longer accepting buffers", m_stage_name);

    auto status = m_frontend->stop();
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to stop frontend");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }
    return AppStatus::SUCCESS;
}

AppStatus FrontendStage::configure(MediaLibraryFrontend &frontend)
{
    if (m_frontend != nullptr)
    {
        m_frontend->stop();
        m_frontend = nullptr;
    }
    return create(frontend);
}

void FrontendStage::loop()
{
    std::unique_lock<std::mutex> lock(m_running_mutex);
    m_running_cv.wait(lock, [this] { return m_end_of_stream == true; });
}

tl::expected<std::vector<frontend_output_stream_t>, media_library_return> FrontendStage::get_outputs_streams()
{
    return m_frontend->get_outputs_streams();
}

FrontendStageBuild::Builder &FrontendStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
FrontendStageBuild::Builder &FrontendStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
FrontendStageBuild::Builder &FrontendStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
FrontendStageBuild::Builder &FrontendStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}
std::shared_ptr<FrontendStage> FrontendStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<FrontendStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

FrontendStageBuild::Builder FrontendStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sources
