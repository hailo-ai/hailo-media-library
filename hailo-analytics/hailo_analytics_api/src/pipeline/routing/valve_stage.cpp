// General includes
#include <stddef.h>
#include <atomic>
#include <memory>
#include <optional>
#include <string>

// Infra includes
#include "hailo_analytics/pipeline/routing/valve_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::routing
{

ValveStage::ValveStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_valve(true)
{
}

AppStatus ValveStage::process(BufferPtr data)
{
    if (m_valve)
    {
        send_to_subscribers(data);
    }
    return AppStatus::SUCCESS;
}
void ValveStage::set_valve(bool valve)
{
    m_valve = valve;
}

// ValveStageBuild::Builder Implementation
ValveStageBuild::Builder &ValveStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

ValveStageBuild::Builder &ValveStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
ValveStageBuild::Builder &ValveStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

ValveStageBuild::Builder &ValveStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<ValveStage> ValveStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<ValveStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

ValveStageBuild::Builder ValveStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::routing
