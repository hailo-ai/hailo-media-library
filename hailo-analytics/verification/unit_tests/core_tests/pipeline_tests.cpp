#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <iostream>

#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "core_tests_common.hpp"

using namespace hailo_analytics::pipeline;
using ::testing::_;
using ::testing::Return;

// ============================================================================
// Pipeline Tests
// ============================================================================

TEST_F(PipelineTest, Constructor)
{
    ASSERT_NO_THROW({ auto pipeline = create_pipeline("pipeline1"); });
}

TEST_F(PipelineTest, AddStageGeneral)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("stage1");

    ASSERT_NO_THROW({ pipeline.add_stage(stage, StageType::GENERAL); });
}

TEST_F(PipelineTest, AddStageSource)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("source1");

    ASSERT_NO_THROW({ pipeline.add_stage(stage, StageType::SOURCE); });
}

TEST_F(PipelineTest, AddStageSink)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("sink1");

    ASSERT_NO_THROW({ pipeline.add_stage(stage, StageType::SINK); });
}

TEST_F(PipelineTest, AddMultipleStages)
{
    Pipeline pipeline("pipeline");
    auto source = create_test_stage("source");
    auto general1 = create_test_stage("general1");
    auto general2 = create_test_stage("general2");
    auto sink = create_test_stage("sink");

    pipeline.add_stage(source, StageType::SOURCE);
    pipeline.add_stage(general1, StageType::GENERAL);
    pipeline.add_stage(general2, StageType::GENERAL);
    pipeline.add_stage(sink, StageType::SINK);

    // Verify by getting stages by name
    EXPECT_EQ(pipeline.get_stage_by_name("source"), source);
    EXPECT_EQ(pipeline.get_stage_by_name("general1"), general1);
    EXPECT_EQ(pipeline.get_stage_by_name("general2"), general2);
    EXPECT_EQ(pipeline.get_stage_by_name("sink"), sink);
}

TEST_F(PipelineTest, SetInStage)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("in_stage");

    ASSERT_NO_THROW({ pipeline.set_in_stage(stage); });
}

TEST_F(PipelineTest, SetOutStage)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("out_stage");

    ASSERT_NO_THROW({ pipeline.set_out_stage(stage); });
}

TEST_F(PipelineTest, GetStageByName)
{
    Pipeline pipeline("pipeline");
    auto stage1 = create_test_stage("stage1");
    auto stage2 = create_test_stage("stage2");

    pipeline.add_stage(stage1);
    pipeline.add_stage(stage2);

    EXPECT_EQ(pipeline.get_stage_by_name("stage1"), stage1);
    EXPECT_EQ(pipeline.get_stage_by_name("stage2"), stage2);
}

TEST_F(PipelineTest, GetStageByNameNotFound)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("stage1");

    pipeline.add_stage(stage);

    EXPECT_EQ(pipeline.get_stage_by_name("nonexistent"), nullptr);
}

TEST_F(PipelineTest, StartAndStop)
{
    Pipeline pipeline("pipeline");
    auto stage = create_test_stage("stage1");

    pipeline.add_stage(stage);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage->get_init_call_count(), 1);
    EXPECT_EQ(stage->get_deinit_call_count(), 1);
}

TEST_F(PipelineTest, StartStopOrder)
{
    Pipeline pipeline("pipeline");
    auto source = create_test_stage("source");
    auto general = create_test_stage("general");
    auto sink = create_test_stage("sink");

    pipeline.add_stage(source, StageType::SOURCE);
    pipeline.add_stage(general, StageType::GENERAL);
    pipeline.add_stage(sink, StageType::SINK);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // All should have started
    EXPECT_EQ(source->get_init_call_count(), 1);
    EXPECT_EQ(general->get_init_call_count(), 1);
    EXPECT_EQ(sink->get_init_call_count(), 1);

    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    // All should have stopped
    EXPECT_EQ(source->get_deinit_call_count(), 1);
    EXPECT_EQ(general->get_deinit_call_count(), 1);
    EXPECT_EQ(sink->get_deinit_call_count(), 1);
}

TEST_F(PipelineTest, PushToInStage)
{
    Pipeline pipeline("pipeline");
    auto in_stage = create_test_stage("in_stage");

    in_stage->add_queue("external");
    pipeline.set_in_stage(in_stage);
    pipeline.add_stage(in_stage);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    pipeline.push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(in_stage->get_process_call_count(), 1);
}

TEST_F(PipelineTest, AddQueueToInStage)
{
    Pipeline pipeline("pipeline");
    auto in_stage = create_test_stage("in_stage");

    pipeline.set_in_stage(in_stage);

    ASSERT_NO_THROW({ pipeline.add_queue("publisher1"); });
}

TEST_F(PipelineTest, SimplePipelineFlow)
{
    Pipeline pipeline("pipeline");
    auto source = create_test_stage("source");
    auto sink = create_test_stage("sink");

    // Connect stages - add_subscriber creates a queue in sink named "source"
    source->add_subscriber(sink);

    pipeline.add_stage(source, StageType::SOURCE);
    pipeline.add_stage(sink, StageType::SINK);
    pipeline.set_in_stage(source);
    pipeline.set_out_stage(sink);

    // Add external queue to source
    pipeline.add_queue("external");

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    pipeline.push(buffer, "external");

    // Give more time for buffer to flow through both stages
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(source->get_process_call_count(), 1);
    EXPECT_EQ(sink->get_process_call_count(), 1);
}

TEST_F(PipelineTest, MultiStagePipelineFlow)
{
    Pipeline pipeline("pipeline");
    auto source = create_test_stage("source");
    auto stage1 = create_test_stage("stage1");
    auto stage2 = create_test_stage("stage2");
    auto sink = create_test_stage("sink");

    // Connect stages
    source->add_queue("external");
    source->add_subscriber(stage1);
    stage1->add_subscriber(stage2);
    stage2->add_subscriber(sink);

    pipeline.add_stage(source, StageType::SOURCE);
    pipeline.add_stage(stage1, StageType::GENERAL);
    pipeline.add_stage(stage2, StageType::GENERAL);
    pipeline.add_stage(sink, StageType::SINK);
    pipeline.set_in_stage(source);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    pipeline.push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(source->get_process_call_count(), 1);
    EXPECT_EQ(stage1->get_process_call_count(), 1);
    EXPECT_EQ(stage2->get_process_call_count(), 1);
    EXPECT_EQ(sink->get_process_call_count(), 1);
}

TEST_F(PipelineTest, PipelineToStageSubscription)
{
    Pipeline pipeline("pipeline");
    auto out_stage = create_test_stage("out_stage");
    auto external_stage = create_test_stage("external_stage");

    out_stage->add_queue("external");

    pipeline.add_stage(out_stage);
    pipeline.set_in_stage(out_stage);
    pipeline.set_out_stage(out_stage);

    // Subscribe external stage to pipeline
    pipeline.add_subscriber(external_stage);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    external_stage->start();

    BufferPtr buffer = create_test_buffer();
    pipeline.push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    external_stage->stop();
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(out_stage->get_process_call_count(), 1);
    EXPECT_EQ(external_stage->get_process_call_count(), 1);
}

TEST_F(PipelineTest, PipelineToPipelineSubscription)
{
    // Create first pipeline
    auto pipeline1 = std::make_shared<Pipeline>("pipeline1");
    auto p1_source = create_test_stage("p1_source");
    auto p1_sink = create_test_stage("p1_sink");

    p1_source->add_queue("external");
    p1_source->add_subscriber(p1_sink);

    pipeline1->add_stage(p1_source, StageType::SOURCE);
    pipeline1->add_stage(p1_sink, StageType::SINK);
    pipeline1->set_in_stage(p1_source);
    pipeline1->set_out_stage(p1_sink);

    // Create second pipeline
    auto pipeline2 = std::make_shared<Pipeline>("pipeline2");
    auto p2_source = create_test_stage("p2_source");
    auto p2_sink = create_test_stage("p2_sink");

    p2_source->add_subscriber(p2_sink);

    pipeline2->add_stage(p2_source, StageType::SOURCE);
    pipeline2->add_stage(p2_sink, StageType::SINK);
    pipeline2->set_in_stage(p2_source);
    pipeline2->set_out_stage(p2_sink);

    // Subscribe pipeline2 to pipeline1
    pipeline1->add_subscriber(pipeline2);

    EXPECT_EQ(pipeline1->start(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline2->start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    pipeline1->push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    EXPECT_EQ(pipeline2->stop(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline1->stop(), AppStatus::SUCCESS);

    // Verify flow through both pipelines
    EXPECT_EQ(p1_source->get_process_call_count(), 1);
    EXPECT_EQ(p1_sink->get_process_call_count(), 1);
    EXPECT_EQ(p2_source->get_process_call_count(), 1);
    EXPECT_EQ(p2_sink->get_process_call_count(), 1);
}

TEST_F(PipelineTest, ChainedPipelinesToPipelines)
{
    // Create three pipelines that chain together
    auto pipeline1 = std::make_shared<Pipeline>("pipeline1");
    auto p1_stage = create_test_stage("p1_stage");
    p1_stage->add_queue("external");
    pipeline1->add_stage(p1_stage);
    pipeline1->set_in_stage(p1_stage);
    pipeline1->set_out_stage(p1_stage);

    auto pipeline2 = std::make_shared<Pipeline>("pipeline2");
    auto p2_stage = create_test_stage("p2_stage");
    pipeline2->add_stage(p2_stage);
    pipeline2->set_in_stage(p2_stage);
    pipeline2->set_out_stage(p2_stage);

    auto pipeline3 = std::make_shared<Pipeline>("pipeline3");
    auto p3_stage = create_test_stage("p3_stage");
    pipeline3->add_stage(p3_stage);
    pipeline3->set_in_stage(p3_stage);
    pipeline3->set_out_stage(p3_stage);

    // Chain: pipeline1 -> pipeline2 -> pipeline3
    pipeline1->add_subscriber(pipeline2);
    pipeline2->add_subscriber(pipeline3);

    EXPECT_EQ(pipeline1->start(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline2->start(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline3->start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    pipeline1->push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(pipeline3->stop(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline2->stop(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline1->stop(), AppStatus::SUCCESS);

    EXPECT_EQ(p1_stage->get_process_call_count(), 1);
    EXPECT_EQ(p2_stage->get_process_call_count(), 1);
    EXPECT_EQ(p3_stage->get_process_call_count(), 1);
}

TEST_F(PipelineTest, PipelineWithBranchingStages)
{
    Pipeline pipeline("pipeline");
    auto source = create_test_stage("source");
    auto branch1 = create_test_stage("branch1");
    auto branch2 = create_test_stage("branch2");

    source->add_queue("external");
    source->add_subscriber(branch1);
    source->add_subscriber(branch2);

    pipeline.add_stage(source, StageType::SOURCE);
    pipeline.add_stage(branch1, StageType::SINK);
    pipeline.add_stage(branch2, StageType::SINK);
    pipeline.set_in_stage(source);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    pipeline.push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(source->get_process_call_count(), 1);
    EXPECT_EQ(branch1->get_process_call_count(), 1);
    EXPECT_EQ(branch2->get_process_call_count(), 1);
}

TEST_F(PipelineTest, PipelinePtr)
{
    PipelinePtr pipeline_ptr = std::make_shared<Pipeline>("pipeline_ptr");
    ASSERT_NE(pipeline_ptr, nullptr);

    auto stage = create_test_stage("stage1");
    pipeline_ptr->add_stage(stage);

    EXPECT_EQ(pipeline_ptr->get_stage_by_name("stage1"), stage);
}

TEST_F(PipelineTest, MultipleBuffersFlowThroughPipeline)
{
    Pipeline pipeline("pipeline");
    auto source = create_test_stage("source");
    auto sink = create_test_stage("sink");

    source->add_queue("external");
    source->add_subscriber(sink);

    pipeline.add_stage(source, StageType::SOURCE);
    pipeline.add_stage(sink, StageType::SINK);
    pipeline.set_in_stage(source);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);

    const int num_buffers = 5;
    for (int i = 0; i < num_buffers; i++)
    {
        BufferPtr buffer = create_test_buffer();
        pipeline.push(buffer, "external");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(source->get_process_call_count(), num_buffers);
    EXPECT_EQ(sink->get_process_call_count(), num_buffers);
}

TEST_F(PipelineTest, EmptyPipelineStartStop)
{
    Pipeline pipeline("pipeline");

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);
}

TEST_F(PipelineTest, PipelineWithOnlySourceStages)
{
    Pipeline pipeline("pipeline");
    auto source1 = create_test_stage("source1");
    auto source2 = create_test_stage("source2");

    pipeline.add_stage(source1, StageType::SOURCE);
    pipeline.add_stage(source2, StageType::SOURCE);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(source1->get_init_call_count(), 1);
    EXPECT_EQ(source2->get_init_call_count(), 1);
}

TEST_F(PipelineTest, PipelineWithOnlySinkStages)
{
    Pipeline pipeline("pipeline");
    auto sink1 = create_test_stage("sink1");
    auto sink2 = create_test_stage("sink2");

    pipeline.add_stage(sink1, StageType::SINK);
    pipeline.add_stage(sink2, StageType::SINK);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(sink1->get_init_call_count(), 1);
    EXPECT_EQ(sink2->get_init_call_count(), 1);
}

TEST_F(PipelineTest, MixedStageTypes)
{
    Pipeline pipeline("pipeline");
    auto source1 = create_test_stage("source1");
    auto source2 = create_test_stage("source2");
    auto general1 = create_test_stage("general1");
    auto general2 = create_test_stage("general2");
    auto sink1 = create_test_stage("sink1");
    auto sink2 = create_test_stage("sink2");

    pipeline.add_stage(source1, StageType::SOURCE);
    pipeline.add_stage(source2, StageType::SOURCE);
    pipeline.add_stage(general1, StageType::GENERAL);
    pipeline.add_stage(general2, StageType::GENERAL);
    pipeline.add_stage(sink1, StageType::SINK);
    pipeline.add_stage(sink2, StageType::SINK);

    EXPECT_EQ(pipeline.start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(pipeline.stop(), AppStatus::SUCCESS);

    // All stages should have started and stopped
    for (auto stage : {source1, source2, general1, general2, sink1, sink2})
    {
        EXPECT_EQ(stage->get_init_call_count(), 1);
        EXPECT_EQ(stage->get_deinit_call_count(), 1);
    }
}

TEST_F(PipelineTest, PipelineAsStageInAnotherPipeline)
{
    // Create an inner pipeline
    auto inner_pipeline = std::make_shared<Pipeline>("inner_pipeline");
    auto inner_stage = create_test_stage("inner_stage");
    inner_pipeline->add_stage(inner_stage);
    inner_pipeline->set_in_stage(inner_stage);
    inner_pipeline->set_out_stage(inner_stage);

    // Create outer pipeline and add inner as a stage
    auto outer_pipeline = std::make_shared<Pipeline>("outer_pipeline");
    auto outer_source = create_test_stage("outer_source");
    auto outer_sink = create_test_stage("outer_sink");

    outer_source->add_queue("external");

    // Connect: outer_source -> inner_pipeline -> outer_sink
    outer_source->add_subscriber(inner_pipeline);
    inner_pipeline->add_subscriber(outer_sink);

    outer_pipeline->add_stage(outer_source, StageType::SOURCE);
    outer_pipeline->add_stage(inner_pipeline, StageType::GENERAL);
    outer_pipeline->add_stage(outer_sink, StageType::SINK);
    outer_pipeline->set_in_stage(outer_source);

    EXPECT_EQ(outer_pipeline->start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    outer_pipeline->push(buffer, "external");

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_EQ(outer_pipeline->stop(), AppStatus::SUCCESS);

    EXPECT_EQ(outer_source->get_process_call_count(), 1);
    EXPECT_EQ(inner_stage->get_process_call_count(), 1);
    EXPECT_EQ(outer_sink->get_process_call_count(), 1);
}
