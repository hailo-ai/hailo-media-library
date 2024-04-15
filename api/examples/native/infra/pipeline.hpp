#pragma once

#include "base.hpp"
#include "stages.hpp"

#include <queue>
#include <thread>
#include <vector>

class Pipeline
{
private:
    std::vector<std::shared_ptr<IStage>> m_stages;
    std::vector<std::thread> m_threads;

public:

    void add_stage(std::shared_ptr<IStage> stage)
    {
        m_stages.push_back(stage);
    }

    void run_pipeline()
    {
        for (auto &stage : m_stages)
        {
            std::thread t(&IStage::loop, stage);
            m_threads.push_back(std::move(t));
        }
    }

    void stop_pipeline()
    {
        for (auto &stage : m_stages)
        {
            stage->set_end_of_stream(true);
        }

        for (auto &t : m_threads)
        {
            t.join();
        }
    }
};