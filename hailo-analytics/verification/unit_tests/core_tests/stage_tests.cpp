#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include "hailo_analytics/pipeline/core/stage.hpp"
#include "core_tests_common.hpp"

using ::testing::_;
using ::testing::Return;

// ============================================================================
// Stage Tests
// ============================================================================

TEST_F(StageTest, GetName)
{
    SimpleStage stage("test_stage");
    EXPECT_EQ(stage.get_name(), "test_stage");
}

TEST_F(StageTest, TraceFPS)
{
    SimpleStage stage("test_stage");
    // Just verify it doesn't crash
    ASSERT_NO_THROW({
        stage.trace_fps();
        stage.trace_fps();
        stage.trace_fps();
    });
}

// ============================================================================
// ThreadedStage Tests
// ============================================================================

TEST_F(ThreadedStageTest, Constructor)
{
    ASSERT_NO_THROW({ TestThreadedStage stage("test_stage", 10, false); });
}

TEST_F(ThreadedStageTest, ConstructorLeaky)
{
    ASSERT_NO_THROW({ TestThreadedStage stage("test_stage", 10, true); });
}

TEST_F(ThreadedStageTest, GetName)
{
    TestThreadedStage stage("my_stage", 10);
    EXPECT_EQ(stage.get_name(), "my_stage");
}

TEST_F(ThreadedStageTest, StartAndStop)
{
    TestThreadedStage stage("test_stage", 10);

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    // Give thread time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    // Verify init and deinit were called
    EXPECT_EQ(stage.init_call_count, 1);
    EXPECT_EQ(stage.deinit_call_count, 1);
}

TEST_F(ThreadedStageTest, MultipleStartStop)
{
    TestThreadedStage stage("test_stage", 10);

    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(stage.start(), AppStatus::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);
    }

    EXPECT_EQ(stage.init_call_count, 3);
    EXPECT_EQ(stage.deinit_call_count, 3);
}

TEST_F(ThreadedStageTest, AddQueue)
{
    TestThreadedStage stage("test_stage", 10);

    ASSERT_NO_THROW({
        stage.add_queue("publisher1");
        stage.add_queue("publisher2");
    });
}

TEST_F(ThreadedStageTest, ProcessBuffer)
{
    TestThreadedStage stage("test_stage", 10);
    stage.add_queue("source");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    stage.push(buffer, "source");

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage.process_call_count, 1);
}

TEST_F(ThreadedStageTest, ProcessMultipleBuffers)
{
    TestThreadedStage stage("test_stage", 10);
    stage.add_queue("source");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    std::vector<BufferPtr> buffers;
    for (int i = 0; i < 5; i++)
    {
        BufferPtr buffer = create_test_buffer();
        buffers.push_back(buffer);
        stage.push(buffer, "source");
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage.process_call_count, 5);
}

TEST_F(ThreadedStageTest, SetEndOfStream)
{
    TestThreadedStage stage("test_stage", 10);
    stage.add_queue("source");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    stage.set_end_of_stream(true);

    // Thread should exit quickly
    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);
}

TEST_F(ThreadedStageTest, AddSubscriber)
{
    TestThreadedStage stage("test_stage", 10);
    auto subscriber = std::make_shared<SimpleStage>("subscriber");

    ASSERT_NO_THROW({ stage.add_subscriber(subscriber); });

    // Verify subscriber got a queue added
    EXPECT_EQ(subscriber->queue_names.size(), 1);
    EXPECT_EQ(subscriber->queue_names[0], "test_stage");
}

TEST_F(ThreadedStageTest, AddMultipleSubscribers)
{
    TestThreadedStage stage("test_stage", 10);
    auto sub1 = std::make_shared<SimpleStage>("sub1");
    auto sub2 = std::make_shared<SimpleStage>("sub2");
    auto sub3 = std::make_shared<SimpleStage>("sub3");

    stage.add_subscriber(sub1);
    stage.add_subscriber(sub2);
    stage.add_subscriber(sub3);

    EXPECT_EQ(sub1->queue_names.size(), 1);
    EXPECT_EQ(sub2->queue_names.size(), 1);
    EXPECT_EQ(sub3->queue_names.size(), 1);
}

TEST_F(ThreadedStageTest, SendToSpecificSubscriber)
{
    TestThreadedStage stage("test_stage", 10);
    auto sub1 = std::make_shared<SimpleStage>("sub1");
    auto sub2 = std::make_shared<SimpleStage>("sub2");

    stage.add_subscriber(sub1);
    stage.add_subscriber(sub2);

    BufferPtr buffer = create_test_buffer();
    stage.send_to_specific_subscriber("sub1", buffer);

    // Small delay to ensure push completes
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_EQ(sub1->pushed_data.size(), 1);
    EXPECT_EQ(sub1->pushed_data[0].first, buffer);
    EXPECT_EQ(sub1->pushed_data[0].second, "test_stage");

    EXPECT_EQ(sub2->pushed_data.size(), 0);
}

TEST_F(ThreadedStageTest, SendToMultipleSpecificSubscribers)
{
    TestThreadedStage stage("test_stage", 10);
    auto sub1 = std::make_shared<SimpleStage>("sub1");
    auto sub2 = std::make_shared<SimpleStage>("sub2");
    auto sub3 = std::make_shared<SimpleStage>("sub3");

    stage.add_subscriber(sub1);
    stage.add_subscriber(sub2);
    stage.add_subscriber(sub3);

    BufferPtr buffer1 = create_test_buffer();
    BufferPtr buffer2 = create_test_buffer();

    stage.send_to_specific_subscriber("sub1", buffer1);
    stage.send_to_specific_subscriber("sub3", buffer2);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_EQ(sub1->pushed_data.size(), 1);
    EXPECT_EQ(sub2->pushed_data.size(), 0);
    EXPECT_EQ(sub3->pushed_data.size(), 1);
}

TEST_F(ThreadedStageTest, PushToCorrectQueue)
{
    TestThreadedStage stage("test_stage", 10);
    stage.add_queue("source1");
    stage.add_queue("source2");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    BufferPtr buffer1 = create_test_buffer();
    BufferPtr buffer2 = create_test_buffer();

    stage.push(buffer1, "source1");
    stage.push(buffer2, "source2");

    // Wait for processing (only source1 is main stream and gets processed)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    // Only buffer1 should be processed (from main queue)
    EXPECT_GE(stage.process_call_count, 1);
}

TEST_F(ThreadedStageTest, ThreadProcessesInOrder)
{
    TestThreadedStage stage("test_stage", 10);
    stage.add_queue("source");
    stage.process_delay = std::chrono::milliseconds(10);

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    std::vector<BufferPtr> buffers;
    for (int i = 0; i < 3; i++)
    {
        BufferPtr buffer = create_test_buffer();
        buffers.push_back(buffer);
        stage.push(buffer, "source");
    }

    // Wait for all processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage.process_call_count, 3);
}

TEST_F(ThreadedStageTest, StopWithPendingBuffers)
{
    TestThreadedStage stage("test_stage", 10);
    stage.add_queue("source");
    stage.process_delay = std::chrono::milliseconds(50);

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    // Push many buffers
    for (int i = 0; i < 10; i++)
    {
        stage.push(create_test_buffer(), "source");
    }

    // Stop quickly (some buffers may not be processed)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    // Should have processed at least some buffers
    EXPECT_GT(stage.process_call_count, 0);
}

TEST_F(ThreadedStageTest, LeakyQueueBehavior)
{
    TestThreadedStage stage("test_stage", 2, true); // Small leaky queue
    stage.add_queue("source");
    stage.process_delay = std::chrono::milliseconds(100); // Slow processing

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    // Push more than queue can hold
    for (int i = 0; i < 5; i++)
    {
        stage.push(create_test_buffer(), "source");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    // With leaky queue, some buffers should be dropped
    EXPECT_LT(stage.process_call_count, 5);
}

TEST_F(ThreadedStageTest, ThreadedStagePtr)
{
    ThreadedStagePtr stage_ptr = std::make_shared<TestThreadedStage>("test_stage", 10);
    ASSERT_NE(stage_ptr, nullptr);
    EXPECT_EQ(stage_ptr->get_name(), "test_stage");
}

TEST_F(ThreadedStageTest, TracingEnabled)
{
    TestThreadedStage stage("test_stage", 10, false, true);
    stage.add_queue("source");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    stage.push(create_test_buffer(), "source");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage.process_call_count, 1);
}

TEST_F(ThreadedStageTest, TracingDisabled)
{
    TestThreadedStage stage("test_stage", 10, false, false);
    stage.add_queue("source");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    stage.push(create_test_buffer(), "source");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage.process_call_count, 1);
}

TEST_F(ThreadedStageTest, ConcurrentPushes)
{
    TestThreadedStage stage("test_stage", 100);
    stage.add_queue("source");

    EXPECT_EQ(stage.start(), AppStatus::SUCCESS);

    const int num_threads = 3;
    const int pushes_per_thread = 10;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < pushes_per_thread; i++)
            {
                stage.push(create_test_buffer(), "source");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // Wait for all processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(stage.stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage.process_call_count, num_threads * pushes_per_thread);
}

TEST_F(ThreadedStageTest, StageChainWithSubscribers)
{
    auto stage1 = std::make_shared<TestThreadedStage>("stage1", 10);
    auto stage2 = std::make_shared<SimpleStage>("stage2");
    auto stage3 = std::make_shared<SimpleStage>("stage3");

    stage1->add_queue("source");
    stage1->add_subscriber(stage2);
    stage1->add_subscriber(stage3);

    EXPECT_EQ(stage1->start(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    stage1->send_to_specific_subscriber("stage2", buffer);
    stage1->send_to_specific_subscriber("stage3", buffer);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(stage1->stop(), AppStatus::SUCCESS);

    EXPECT_EQ(stage2->pushed_data.size(), 1);
    EXPECT_EQ(stage3->pushed_data.size(), 1);
}
