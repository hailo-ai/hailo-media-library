#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace hailo_analytics::pipeline
{

// Stage class implementation
Stage::Stage(std::string name, bool trace_processing_operations)
    : m_stage_name(name), m_tracing(std::make_unique<StageTracing>(name)),
      m_trace_processing_operations(trace_processing_operations)
{
}

std::string Stage::get_name() const
{
    return m_stage_name;
}

void Stage::trace_fps()
{
    m_tracing->increment_counter();
}

// ThreadedStage class implementation
ThreadedStage::ThreadedStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : Stage(name, trace_processing_operations), m_queue_size(queue_size), m_leaky(leaky)
{
}

AppStatus ThreadedStage::start()
{
    set_end_of_stream(false);

    // Initialize stage before starting thread
    auto init_status = init();
    if (init_status != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Stage {} failed to initialize", m_stage_name);
        return init_status;
    }

    m_thread = std::thread(&ThreadedStage::loop, this);
#if defined(__linux__)
    // Set thread name to stage name
    pthread_setname_np(m_thread.native_handle(), m_stage_name.substr(0, 15).c_str());
#endif
    return AppStatus::SUCCESS;
}

AppStatus ThreadedStage::stop()
{
    set_end_of_stream(true);
    if (m_thread.joinable())
    {
        m_thread.join();
    }

    // Deinitialize stage after thread has stopped
    auto deinit_status = deinit();
    if (deinit_status != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Stage {} failed to deinitialize", m_stage_name);
    }

    return deinit_status;
}

void ThreadedStage::add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id)
{
    (void)stream_id; // Currently not used in this implementation
    m_subscribers.push_back(subscriber);
    subscriber->add_queue(m_stage_name);
}

void ThreadedStage::add_queue(std::string publisher_name)
{
    m_queues.push_back(std::make_shared<Queue>(m_stage_name, publisher_name, m_queue_size, m_leaky));
}

AppStatus ThreadedStage::init()
{
    return AppStatus::SUCCESS;
}

AppStatus ThreadedStage::deinit()
{
    return AppStatus::SUCCESS;
}

AppStatus ThreadedStage::process(BufferPtr buffer)
{
    (void)buffer;
    return AppStatus::SUCCESS;
}

void ThreadedStage::loop()
{
    while (!m_end_of_stream)
    {
        // The first connected queue is always considered "main stream"
        if (m_queues.size() == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        BufferPtr data = m_queues[0]->pop();
        if (data == nullptr && m_end_of_stream.load())
        {
            break;
        }
        else if (data == nullptr)
        {
            HAILO_ANALYTICS_LOG_WARN("Stage {} received a null buffer, skipping.", m_stage_name);
            continue;
        }

        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_start(data);
        }

        auto status = process(data);
        if (status != AppStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_WARN("Stage {} failed to process buffer", m_stage_name);
        }

        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_end(data);
        }

        trace_fps();
    }
}

void ThreadedStage::push(BufferPtr data, std::string publisher_name)
{
    for (auto &queue : m_queues)
    {
        if (queue->name() == publisher_name)
        {
            queue->push(data);
            break;
        }
    }
}

void ThreadedStage::send_to_subscribers(BufferPtr data)
{
    for (auto &subscriber : m_subscribers)
    {
        subscriber->push(data, m_stage_name);
    }
}

void ThreadedStage::send_to_specific_subscriber(std::string stage_name, BufferPtr data)
{
    for (auto &subscriber : m_subscribers)
    {
        if (stage_name == subscriber->get_name())
        {
            subscriber->push(data, m_stage_name);
        }
    }
}

void ThreadedStage::set_end_of_stream(bool end_of_stream)
{
    m_end_of_stream = end_of_stream;
    if (end_of_stream)
    {
        for (auto &queue : m_queues)
        {
            queue->flush();
        }
    }
    else
    {
        for (auto &queue : m_queues)
        {
            queue->reset();
        }
    }
}

} // namespace hailo_analytics::pipeline
