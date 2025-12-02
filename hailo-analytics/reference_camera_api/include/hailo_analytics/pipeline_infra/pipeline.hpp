#pragma once

// general includes
#include <vector>

// infra includes
#include "stage.hpp"

enum class StageType
{
    GENERAL = 0,
    SOURCE,
    SINK
};

class Pipeline;
using PipelinePtr = std::shared_ptr<Pipeline>;

class Pipeline : public Stage
{
  private:
    std::vector<StagePtr> m_stages;      // All stages, used for full queries (get and print)
    std::vector<StagePtr> m_gen_stages;  // For general type stages
    std::vector<StagePtr> m_src_stages;  // For source type stages
    std::vector<StagePtr> m_sink_stages; // For sink type stages

    StagePtr m_in_stage;  // The stage that will subscribe to external sources
    StagePtr m_out_stage; // The stage that will publish to external sinks

  public:
    void add_stage(StagePtr stage, StageType type = StageType::GENERAL);

    void set_in_stage(StagePtr stage);  // Set which stage is the input to this pipeline
    void set_out_stage(StagePtr stage); // Set which stage is the output to this pipeline

    // Overrides
    AppStatus start() override;
    AppStatus stop() override;
    // Subscribes the m_in_stage of subscriber pipeline to m_out_stage of this pipeline
    void add_subscriber(StagePtr subscriber) override;
    void add_queue(std::string publisher_name) override;
    void push(BufferPtr data, std::string publisher_name) override;

    StagePtr get_stage_by_name(std::string stage_name);
};
