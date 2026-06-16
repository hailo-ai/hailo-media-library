#include "hailo_analytics/pipeline/core/pipeline.hpp"

#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline
{

Pipeline::Pipeline(std::string name, bool trace_processing_operations) : Stage(name, trace_processing_operations)
{
}

void Pipeline::add_stage(StagePtr stage, StageType type)
{
    switch (type)
    {
    case StageType::SOURCE:
        m_src_stages.push_back(stage);
        break;
    case StageType::SINK:
        m_sink_stages.push_back(stage);
        break;
    default:
        m_gen_stages.push_back(stage);
    }
    m_stages.push_back(stage);
}

void Pipeline::set_in_stage(StagePtr stage)
{
    m_in_stage = stage;
}

void Pipeline::set_out_stage(StagePtr stage)
{
    m_out_stage = stage;
}

AppStatus Pipeline::start()
{
    // Start the sink stages
    for (auto &stage : m_sink_stages)
    {
        AppStatus status = stage->start();
        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
    }

    // Start the general stages
    for (auto &stage : m_gen_stages)
    {
        AppStatus status = stage->start();
        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
    }

    // Start the source stages
    for (auto &stage : m_src_stages)
    {
        AppStatus status = stage->start();
        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
    }
    return AppStatus::SUCCESS;
}

AppStatus Pipeline::stop()
{
    // Stop the source stages
    for (auto &stage : m_src_stages)
    {
        AppStatus status = stage->stop();
        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
    }

    // Stop the general stages
    for (auto &stage : m_gen_stages)
    {
        AppStatus status = stage->stop();
        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
    }

    // Stop the sink stages
    for (auto &stage : m_sink_stages)
    {
        AppStatus status = stage->stop();
        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
    }
    return AppStatus::SUCCESS;
}

void Pipeline::add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id)
{
    m_out_stage->add_subscriber(subscriber, stream_id);
}

void Pipeline::add_queue(std::string publisher_name)
{
    m_in_stage->add_queue(publisher_name);
}

void Pipeline::push(BufferPtr data, std::string publisher_name)
{
    m_in_stage->push(data, publisher_name);
}

StagePtr Pipeline::get_stage_by_name(std::string stage_name)
{
    for (auto &stage : m_stages)
    {
        if (stage->get_name() == stage_name)
        {
            return stage;
        }
    }
    return nullptr;
}

const std::vector<StagePtr> &Pipeline::get_stages() const
{
    return m_stages;
}

StagePtr Pipeline::get_in_stage() const
{
    return m_in_stage;
}

StagePtr Pipeline::get_out_stage() const
{
    return m_out_stage;
}

} // namespace hailo_analytics::pipeline
