#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/routing/tee_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::routing
{
TeeStage::TeeStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
{
}

AppStatus TeeStage::process(BufferPtr data)
{
    for (auto &subscriber : m_subscribers)
    {
        // The copy constractor performs a shallow copy
        BufferPtr new_buffer = std::make_shared<Buffer>(*data);
        subscriber->push(new_buffer, m_stage_name);
    }

    return AppStatus::SUCCESS;
}

// TeeStageBuild::Builder Implementation
TeeStageBuild::Builder &TeeStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

TeeStageBuild::Builder &TeeStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

TeeStageBuild::Builder &TeeStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

TeeStageBuild::Builder &TeeStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<TeeStage> TeeStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<TeeStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

TeeStageBuild::Builder TeeStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::routing
