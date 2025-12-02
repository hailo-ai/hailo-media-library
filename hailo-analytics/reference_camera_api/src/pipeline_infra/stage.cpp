#include "hailo_analytics/pipeline_infra/stage.hpp"
#include "hailo_analytics/pipeline_infra/queue.hpp"

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
    m_end_of_stream = false;
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
    m_thread.join();
    return AppStatus::SUCCESS;
}

void ThreadedStage::add_subscriber(StagePtr subscriber)
{
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
    init();

    while (!m_end_of_stream)
    {
        // The first connected queue is always considered "main stream"
        BufferPtr data = m_queues[0]->pop();
        if (data == nullptr && m_end_of_stream)
        {
            break;
        }

        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_start();
        }

        process(data);

        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_end();
        }

        trace_fps();
    }

    deinit();
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
}
