#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::routing
{

/**
 * @brief Constructs a CallbackStage with the specified configuration.
 */
CallbackStage::CallbackStage(std::string name, size_t queue_size, bool leaky, std::function<void(BufferPtr)> callback,
                             bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_callback(callback)
{
}

/**
 * @brief Processes a buffer by executing the callback (if set) and forwarding to subscribers.
 */
AppStatus CallbackStage::process(BufferPtr data)
{
    if (m_callback)
        m_callback(data);

    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

/**
 * @brief Sets or updates the callback function to be executed on each buffer.
 */
void CallbackStage::set_callback(std::function<void(BufferPtr)> callback)
{
    m_callback = callback;
}

// CallbackStageBuild::Builder Implementation
CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<CallbackStage> CallbackStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<CallbackStage>(m_stage_name.value(), m_queue_size, m_leaky, nullptr, m_trace);
}

CallbackStageBuild::Builder CallbackStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::routing
