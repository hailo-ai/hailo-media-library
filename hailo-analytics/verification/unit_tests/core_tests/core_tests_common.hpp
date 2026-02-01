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
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
};

// ============================================================================
// Buffer Classes
// ============================================================================

class BufferTest : public ::testing::Test
{
  protected:
    HailoMediaLibraryBufferPtr mock_buffer;

    void SetUp() override
    {
        // Create a mock buffer pointer
        mock_buffer = std::make_shared<hailo_media_library_buffer>();
    }

    void TearDown() override
    {
        mock_buffer.reset();
    }
};

// ============================================================================
// Queue Classes
// ============================================================================

class QueueTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
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

    SimpleStage(std::string name) : Stage(name)
    {
    }

    AppStatus start() override
    {
        return AppStatus::SUCCESS;
    }
    AppStatus stop() override
    {
        return AppStatus::SUCCESS;
    }

    void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) override
    {
        // Not used in simple stage
        (void)subscriber;
        (void)stream_id;
    }

    void add_queue(std::string publisher_name) override
    {
        std::lock_guard<std::mutex> lock(data_mutex);
        queue_names.push_back(publisher_name);
    }

    void push(BufferPtr data, std::string publisher_name) override
    {
        std::lock_guard<std::mutex> lock(data_mutex);
        pushed_data.push_back({data, publisher_name});
    }
};

class StageTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
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

    TestThreadedStage(std::string name, size_t queue_size, bool leaky = false, bool trace_processing = true)
        : ThreadedStage(name, queue_size, leaky, trace_processing)
    {
    }

    AppStatus init() override
    {
        init_call_count++;
        return init_return_status;
    }

    AppStatus deinit() override
    {
        deinit_call_count++;
        return deinit_return_status;
    }

    AppStatus process(BufferPtr buffer) override
    {
        process_call_count++;
        if (process_delay.count() > 0)
        {
            std::this_thread::sleep_for(process_delay);
        }
        send_to_subscribers(buffer);
        // Buffer is released here when it goes out of scope, returning to pool
        return process_return_status;
    }

    int get_init_call_count()
    {
        return init_call_count.load();
    }

    int get_deinit_call_count()
    {
        return deinit_call_count.load();
    }

    int get_process_call_count()
    {
        return process_call_count.load();
    }
};

class ThreadedStageTest : public ::testing::Test
{
  protected:
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    void SetUp() override
    {
    }
    void TearDown() override
    {
    }

    BufferPtr create_test_buffer()
    {
        return std::make_shared<Buffer>(mock_buffer);
    }
};

// ============================================================================
// Pipeline Classes
// ============================================================================

class PipelineTest : public ::testing::Test
{
  protected:
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;

    void SetUp() override
    {
    }
    void TearDown() override
    {
    }

    BufferPtr create_test_buffer()
    {
        return std::make_shared<Buffer>(mock_buffer);
    }

    std::shared_ptr<TestThreadedStage> create_test_stage(std::string name, size_t queue_size = 10)
    {
        return std::make_shared<TestThreadedStage>(name, queue_size);
    }

    std::shared_ptr<Pipeline> create_pipeline(std::string name = "test_pipeline")
    {
        return std::make_shared<Pipeline>(name);
    }
};
