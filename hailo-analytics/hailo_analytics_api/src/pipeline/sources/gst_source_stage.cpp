#include "hailo_analytics/pipeline/sources/gst_source_stage.hpp"

#include <gstmedialibptrs.hpp>
#include <media_library/buffer_pool.hpp>
#include <media_library/media_library_types.hpp>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "buffer_utils.hpp"
#include "gstmedialibcommon.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

static constexpr GstClockTime PULL_TIMEOUT_NS = 100 * GST_MSECOND;

namespace hailo_analytics::pipeline::sources
{

GstSourceStage::GstSourceStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations), m_started(false)
{
}

GstSourceStage::~GstSourceStage()
{
    m_stream_subscribers.clear();
}

AppStatus GstSourceStage::add_appsink(output_stream_id_t stream_id, GstElement *appsink)
{
    if (!appsink || !GST_IS_APP_SINK(appsink))
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSourceStage '{}': invalid appsink element for stream '{}'", m_stage_name,
                                  stream_id);
        return AppStatus::INVALID_ARGUMENT;
    }

    if (m_appsinks.count(stream_id))
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSourceStage '{}': stream '{}' already registered", m_stage_name, stream_id);
        return AppStatus::INVALID_ARGUMENT;
    }

    m_appsinks[stream_id] = GST_APP_SINK(appsink);
    HAILO_ANALYTICS_LOG_INFO("GstSourceStage '{}': registered appsink for stream '{}'", m_stage_name, stream_id);
    return AppStatus::SUCCESS;
}

void GstSourceStage::add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id)
{
    if (!stream_id.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSourceStage '{}': stream_id must be provided when subscribing", m_stage_name);
        throw std::invalid_argument("Stream ID must be provided when subscribing to GstSourceStage");
    }
    auto status = subscribe_to_stream(stream_id.value(), subscriber);
    if (status != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSourceStage '{}': failed to subscribe to stream '{}'", m_stage_name,
                                  stream_id.value());
        throw std::runtime_error("Failed to subscribe to stream");
    }
}

AppStatus GstSourceStage::subscribe_to_stream(output_stream_id_t stream_id, StagePtr subscriber)
{
    if (m_appsinks.find(stream_id) == m_appsinks.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("GstSourceStage '{}': stream '{}' not found in registered appsinks", m_stage_name,
                                  stream_id);
        return AppStatus::INVALID_ARGUMENT;
    }

    m_stream_subscribers[stream_id].push_back(subscriber);
    subscriber->add_queue(stream_id);
    HAILO_ANALYTICS_LOG_INFO("GstSourceStage '{}': subscribed stage to stream '{}'", m_stage_name, stream_id);
    return AppStatus::SUCCESS;
}

AppStatus GstSourceStage::init()
{
    m_started.store(true);
    HAILO_ANALYTICS_LOG_INFO("GstSourceStage '{}': started — now accepting buffers", m_stage_name);
    return AppStatus::SUCCESS;
}

AppStatus GstSourceStage::deinit()
{
    m_started.store(false);
    HAILO_ANALYTICS_LOG_INFO("GstSourceStage '{}': stopped — no longer accepting buffers", m_stage_name);
    return AppStatus::SUCCESS;
}

AppStatus GstSourceStage::stop()
{
    set_end_of_stream(true);
    m_running_cv.notify_one();

    // Join all pull threads
    for (auto &thread : m_pull_threads)
    {
        if (thread.joinable())
            thread.join();
    }
    m_pull_threads.clear();

    return deinit();
}

void GstSourceStage::loop()
{
    // Spawn one pull thread per registered appsink
    for (auto &[stream_id, appsink] : m_appsinks)
    {
        m_pull_threads.emplace_back([this, sid = stream_id, as = appsink]() { pull_loop(sid, as); });
    }

    // Wait until stop() is called
    std::unique_lock<std::mutex> lock(m_running_mutex);
    m_running_cv.wait(lock, [this] { return m_end_of_stream == true; });
}

void GstSourceStage::pull_loop(const std::string &stream_id, GstAppSink *appsink)
{
    HAILO_ANALYTICS_LOG_INFO("GstSourceStage '{}': pull thread started for stream '{}'", m_stage_name, stream_id);

    while (!m_end_of_stream)
    {
        if (!m_started.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        GstSamplePtr sample = gst_app_sink_try_pull_sample(appsink, PULL_TIMEOUT_NS);
        if (!sample)
            continue;

        GstBufferPtr buffer = glib_cpp::ptrs::get_buffer_from_sample(sample);
        GstCaps *caps = gst_sample_get_caps(sample);

        if (!buffer)
            continue;

        // Convert GstBuffer → HailoMediaLibraryBufferPtr
        // Fast path: GstHailoBufferMeta carries the pointer directly (from gsthailovision)
        HailoMediaLibraryBufferPtr hailo_buffer = hailo_buffer_from_gst_buffer(buffer, caps);
        if (!hailo_buffer)
        {
            HAILO_ANALYTICS_LOG_WARN("GstSourceStage '{}': failed to convert GstBuffer for stream '{}'", m_stage_name,
                                     stream_id);
            continue;
        }

        HAILO_ANALYTICS_LOG_DEBUG("GstSourceStage '{}': converted buffer for stream '{}', size={}x{}", m_stage_name,
                                  stream_id, hailo_buffer->owner->get_width(), hailo_buffer->owner->get_height());

        // Wrap in analytics Buffer (creates full-frame ROI)
        BufferPtr analytics_buffer = std::make_shared<Buffer>(hailo_buffer);

        // Push to all subscribers for this stream
        auto it = m_stream_subscribers.find(stream_id);
        if (it != m_stream_subscribers.end())
        {
            HAILO_ANALYTICS_LOG_DEBUG("GstSourceStage '{}': pushing to {} subscribers for stream '{}'", m_stage_name,
                                      it->second.size(), stream_id);
            for (auto &subscriber : it->second)
            {
                subscriber->push(analytics_buffer, stream_id);
            }
        }
        else
        {
            HAILO_ANALYTICS_LOG_WARN("GstSourceStage '{}': no subscribers for stream '{}'", m_stage_name, stream_id);
        }
    }

    HAILO_ANALYTICS_LOG_INFO("GstSourceStage '{}': pull thread stopped for stream '{}'", m_stage_name, stream_id);
}

GstSourceStageBuild::Builder &GstSourceStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = std::move(name);
    return *this;
}

GstSourceStageBuild::Builder &GstSourceStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

GstSourceStageBuild::Builder &GstSourceStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

GstSourceStageBuild::Builder &GstSourceStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<GstSourceStage> GstSourceStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    return std::make_shared<GstSourceStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

GstSourceStageBuild::Builder GstSourceStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sources
