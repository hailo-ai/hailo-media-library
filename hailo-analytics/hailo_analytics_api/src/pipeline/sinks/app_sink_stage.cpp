// General includes
#include <stddef.h>
#include <functional>
#include <memory>
#include <string>
#include <optional>

// Infra includes
#include "hailo_analytics/pipeline/sinks/app_sink_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::sinks
{

AppSinkStage::AppSinkStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_process_func(nullptr)
{
}

AppStatus AppSinkStage::init()
{
    if (!m_process_func)
    {
        HAILO_ANALYTICS_LOG_ERROR("Process function not configured. Call configure()");
        return AppStatus::UNINITIALIZED;
    }
    return AppStatus::SUCCESS;
}

AppStatus AppSinkStage::deinit()
{
    m_process_func = nullptr;
    return AppStatus::SUCCESS;
}

AppStatus AppSinkStage::configure(std::function<void(BufferPtr)> process_func)
{
    m_process_func = process_func;
    return AppStatus::SUCCESS;
}

AppStatus AppSinkStage::process(BufferPtr data)
{
    m_process_func(data);
    return AppStatus::SUCCESS;
}

// AppSinkStageBuild::Builder Implementation
AppSinkStageBuild::Builder &AppSinkStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
AppSinkStageBuild::Builder &AppSinkStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
AppSinkStageBuild::Builder &AppSinkStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

AppSinkStageBuild::Builder &AppSinkStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

AppSinkStageBuild::Builder &AppSinkStageBuild::Builder::set_process_func(std::function<void(BufferPtr)> func)
{
    m_process_func = func;
    return *this;
}

std::shared_ptr<AppSinkStage> AppSinkStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    auto app_sink = std::make_shared<AppSinkStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
    if (m_process_func)
    {
        app_sink->configure(m_process_func);
    }
    return app_sink;
}

AppSinkStageBuild::Builder AppSinkStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sinks
