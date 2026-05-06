#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <iostream>

#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

using namespace hailo_analytics::pipeline;

// ============================================================================
// Metadata Classes
// ============================================================================

class MetadataTest : public ::testing::Test
{
  protected:
    void SetUp() override;
    void TearDown() override;
};

// ============================================================================
// Buffer Classes
// ============================================================================

class BufferTest : public ::testing::Test
{
  protected:
    HailoMediaLibraryBufferPtr mock_buffer;

    void SetUp() override;
    void TearDown() override;
};

// ============================================================================
// Queue Classes
// ============================================================================

class QueueTest : public ::testing::Test
{
  protected:
    void SetUp() override;
    void TearDown() override;
};

// ============================================================================
// Stage Classes
// ============================================================================

// Simple stage implementation for subscriber tests
class SimpleStage : public Stage
{
  public:
    std::vector<std::pair<BufferPtr, std::string>> pushed_data;
    std::mutex data_mutex;
    std::vector<std::string> queue_names;

    SimpleStage(std::string name);
    AppStatus start() override;
    AppStatus stop() override;
    void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) override;
    void add_queue(std::string publisher_name) override;
    void push(BufferPtr data, std::string publisher_name) override;
};

class StageTest : public ::testing::Test
{
  protected:
    void SetUp() override;
    void TearDown() override;
};

// Concrete implementation of ThreadedStage for testing
class TestThreadedStage : public ThreadedStage
{
  public:
    std::atomic<int> init_call_count{0};
    std::atomic<int> deinit_call_count{0};
    std::atomic<int> process_call_count{0};
    AppStatus init_return_status = AppStatus::SUCCESS;
    AppStatus deinit_return_status = AppStatus::SUCCESS;
    AppStatus process_return_status = AppStatus::SUCCESS;
    std::chrono::milliseconds process_delay{0};

    TestThreadedStage(std::string name, size_t queue_size, bool leaky = false, bool trace_processing = true);
    AppStatus init() override;
    AppStatus deinit() override;
    AppStatus process(BufferPtr buffer) override;
    int get_init_call_count();
    int get_deinit_call_count();
    int get_process_call_count();
};

class ThreadedStageTest : public ::testing::Test
{
  protected:
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    void SetUp() override;
    void TearDown() override;
    BufferPtr create_test_buffer();
};

// ============================================================================
// Pipeline Classes
// ============================================================================

class PipelineTest : public ::testing::Test
{
  protected:
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    void SetUp() override;
    void TearDown() override;
    BufferPtr create_test_buffer();
    std::shared_ptr<TestThreadedStage> create_test_stage(std::string name, size_t queue_size = 10);
    std::shared_ptr<Pipeline> create_pipeline(std::string name = "test_pipeline");
};
