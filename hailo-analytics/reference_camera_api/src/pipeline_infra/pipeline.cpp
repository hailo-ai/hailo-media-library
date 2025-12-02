#include "hailo_analytics/pipeline_infra/pipeline.hpp"
#include "hailo_analytics/perfetto/hailo_analytics_perfetto.hpp"

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
        stage->start();
    }

    // Start the general stages
    for (auto &stage : m_gen_stages)
    {
        stage->start();
    }

    // Start the source stages
    for (auto &stage : m_src_stages)
    {
        stage->start();
    }
    return AppStatus::SUCCESS;
}

AppStatus Pipeline::stop()
{
    // Stop the source stages
    for (auto &stage : m_src_stages)
    {
        stage->stop();
    }

    // Stop the general stages
    for (auto &stage : m_gen_stages)
    {
        stage->stop();
    }

    // Stop the sink stages
    for (auto &stage : m_sink_stages)
    {
        stage->stop();
    }
    return AppStatus::SUCCESS;
}

void Pipeline::add_subscriber(StagePtr subscriber)
{
    m_out_stage->add_subscriber(subscriber);
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
