#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include <media_library/buffer_pool.hpp>

namespace hailo_analytics::pipeline
{

// Internal class for Perfetto tracing to maintain ABI compatibility
class QueueTracing
{
  private:
    std::string m_counter_name;
#ifdef HAVE_PERFETTO
    perfetto::DynamicString m_frame_dropped_event_name;
#else
    std::string m_frame_dropped_event_name;
#endif

  public:
    QueueTracing(const std::string &parent_name, const std::string &queue_name, size_t max_buffers)
        : m_counter_name("queue_" + parent_name + "_" + queue_name + "_" + std::to_string(max_buffers)),
          m_frame_dropped_event_name(
#ifdef HAVE_PERFETTO
              perfetto::DynamicString(queue_name + "_frame_dropped")
#else
              queue_name + "_frame_dropped"
#endif
          )
    {
    }

    void track_queue_size(size_t size)
    {
        HAILO_ANALYTICS_TRACE_COUNTER(m_counter_name, size, HAILO_ANALYTICS_QUEUE_LEVEL_TRACK,
                                      HAILO_ANALYTICS_CATEGORY);
    }

    void track_frame_dropped(uint64_t isp_timestamp_ns)
    {
        HAILO_ANALYTICS_TRACE_EVENT(m_frame_dropped_event_name, HAILO_ANALYTICS_PROCESSING_TRACK,
                                    HAILO_ANALYTICS_DETAILED_CATEGORY, "isp_timestamp_ms", isp_timestamp_ns / 1000000);
    }
};

Queue::Queue(std::string parent_name, std::string queue_name, size_t max_buffers, bool leaky)
    : m_max_buffers(max_buffers), m_leaky(leaky), m_name(queue_name), m_flushing(false)
{
    m_mutex = std::make_shared<std::mutex>();
    m_condvar = std::make_unique<std::condition_variable>();
    m_queue = std::queue<BufferPtr>();
    m_tracing = std::make_unique<QueueTracing>(parent_name, queue_name, max_buffers);
}

Queue::~Queue()
{
    m_flushing = true;
    m_condvar->notify_all();
    flush();
}

std::string Queue::name()
{
    return m_name;
}

int Queue::size()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    return m_queue.size();
}

void Queue::push(BufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    if (m_flushing)
    {
        return;
    }
    if (!m_leaky)
    {
        // if not leaky, then wait until there is space in the queue
        m_condvar->wait(lock, [this] { return m_queue.size() < m_max_buffers; });
    }
    else
    {
        // if leaky, pop the front for a full queue
        if (m_queue.size() >= m_max_buffers)
        {

            HailoMediaLibraryBufferPtr buffer = m_queue.front()->get_buffer();
            if (buffer != nullptr)
            {
                m_tracing->track_frame_dropped(buffer->isp_timestamp_ns);
            }
            m_queue.pop();
        }
    }
    m_queue.push(buffer);
    m_tracing->track_queue_size(m_queue.size());

    m_condvar->notify_one();
}

BufferPtr Queue::pop()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    // wait for there to be something in the queue to pull
    m_condvar->wait(lock, [this] { return !m_queue.empty() || m_flushing == true; });
    if (m_queue.empty())
    {
        // if we reached here, then the queue is empty and we are flushing
        return nullptr;
    }
    BufferPtr buffer = m_queue.front();
    m_queue.pop();
    m_tracing->track_queue_size(m_queue.size());

    m_condvar->notify_one();
    return buffer;
}

uint64_t Queue::check_timestamp(std::optional<std::chrono::milliseconds> timeout)
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    // wait for there to be something in the queue to check
    if (timeout.has_value())
    {
        if (m_condvar->wait_for(lock, timeout.value(), [this] { return !m_queue.empty() || m_flushing == true; }) ==
            false)
        {
            // if we reached here, then we timed out
            return 0;
        }
    }
    else
    {
        m_condvar->wait(lock, [this] { return !m_queue.empty() || m_flushing == true; });
    }
    if (m_queue.empty())
    {
        // if we reached here, then the queue is empty and we are flushing
        return 0;
    }
    return m_queue.front()->get_buffer()->isp_timestamp_ns;
}

void Queue::flush()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    m_flushing = true;
    while (!m_queue.empty())
    {
        m_queue.pop();
    }
    m_condvar->notify_all();
}

void Queue::reset()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    m_flushing = false;
    while (!m_queue.empty())
    {
        m_queue.pop();
    }
    m_condvar->notify_all();
}

} // namespace hailo_analytics::pipeline
