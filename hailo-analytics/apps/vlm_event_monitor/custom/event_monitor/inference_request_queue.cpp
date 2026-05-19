#include "inference_request_queue.hpp"

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_event_monitor
{

InferenceRequestQueue::InferenceRequestQueue(std::shared_ptr<VlmInferenceManager> manager)
    : m_manager(std::move(manager))
{
}

InferenceRequestQueue::~InferenceRequestQueue()
{
    stop();
}

void InferenceRequestQueue::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
    {
        return;
    }
    m_worker = std::thread(&InferenceRequestQueue::worker_loop, this);
}

void InferenceRequestQueue::stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
    {
        return;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &job : m_event_queue)
        {
            if (job.on_complete)
            {
                job.on_complete(tl::make_unexpected("InferenceRequestQueue stopped"));
            }
        }
        for (auto &job : m_chat_queue)
        {
            if (job.on_complete)
            {
                job.on_complete(tl::make_unexpected("InferenceRequestQueue stopped"));
            }
        }
        m_event_queue.clear();
        m_chat_queue.clear();
    }
}

bool InferenceRequestQueue::submit(InferenceJob job)
{
    if (job.priority == InferencePriority::EventCheck && !m_event_inference_enabled.load())
    {
        HAILO_ANALYTICS_LOG_DEBUG("InferenceRequestQueue: dropping EventCheck (disabled)");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (job.priority == InferencePriority::EventCheck)
        {
            m_event_queue.push_back(std::move(job));
        }
        else
        {
            m_chat_queue.push_back(std::move(job));
        }
    }
    m_cv.notify_one();
    return true;
}

void InferenceRequestQueue::set_event_inference_enabled(bool enabled)
{
    m_event_inference_enabled.store(enabled);
    HAILO_ANALYTICS_LOG_INFO("InferenceRequestQueue: event_inference_enabled={}", enabled);
}

bool InferenceRequestQueue::is_event_inference_enabled() const
{
    return m_event_inference_enabled.load();
}

size_t InferenceRequestQueue::pending_count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_event_queue.size() + m_chat_queue.size();
}

std::string InferenceRequestQueue::busy_with() const
{
    switch (m_current_busy_state.load())
    {
    case 1:
        return "event_check";
    case 2:
        return "chat";
    default:
        return "idle";
    }
}

void InferenceRequestQueue::worker_loop()
{
    HAILO_ANALYTICS_LOG_INFO("InferenceRequestQueue: worker thread started");
    while (m_running.load())
    {
        InferenceJob job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running.load() || !m_event_queue.empty() || !m_chat_queue.empty(); });
            if (!m_running.load())
            {
                break;
            }
            // Always drain EventCheck before Chat.
            if (!m_event_queue.empty())
            {
                job = std::move(m_event_queue.front());
                m_event_queue.pop_front();
            }
            else if (!m_chat_queue.empty())
            {
                job = std::move(m_chat_queue.front());
                m_chat_queue.pop_front();
            }
            else
            {
                continue;
            }
        }

        m_current_busy_state.store(job.priority == InferencePriority::EventCheck ? 1 : 2);

        tl::expected<InferenceResult, std::string> result =
            tl::make_unexpected("InferenceRequestQueue: job had no work");
        if (job.work)
        {
            try
            {
                result = job.work(*m_manager);
            }
            catch (const std::exception &exception)
            {
                result = tl::make_unexpected(std::string("InferenceRequestQueue: job threw: ") + exception.what());
            }
        }

        m_current_busy_state.store(0);

        if (job.on_complete)
        {
            try
            {
                job.on_complete(std::move(result));
            }
            catch (const std::exception &exception)
            {
                HAILO_ANALYTICS_LOG_ERROR("InferenceRequestQueue: on_complete threw: {}", exception.what());
            }
        }
    }
    HAILO_ANALYTICS_LOG_INFO("InferenceRequestQueue: worker thread stopped");
}

} // namespace vlm_event_monitor
