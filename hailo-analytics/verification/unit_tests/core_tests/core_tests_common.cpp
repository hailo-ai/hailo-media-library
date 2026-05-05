#include "core_tests_common.hpp"

// ============================================================================
// MetadataTest
// ============================================================================

void MetadataTest::SetUp()
{
}
void MetadataTest::TearDown()
{
}

// ============================================================================
// BufferTest
// ============================================================================

void BufferTest::SetUp()
{
    // Create a mock buffer pointer
    mock_buffer = std::make_shared<hailo_media_library_buffer>();
}

void BufferTest::TearDown()
{
    mock_buffer.reset();
}

// ============================================================================
// QueueTest
// ============================================================================

void QueueTest::SetUp()
{
}
void QueueTest::TearDown()
{
}

// ============================================================================
// SimpleStage
// ============================================================================

SimpleStage::SimpleStage(std::string name) : Stage(name)
{
}

AppStatus SimpleStage::start()
{
    return AppStatus::SUCCESS;
}

AppStatus SimpleStage::stop()
{
    return AppStatus::SUCCESS;
}

void SimpleStage::add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id)
{
    // Not used in simple stage
    (void)subscriber;
    (void)stream_id;
}

void SimpleStage::add_queue(std::string publisher_name)
{
    std::lock_guard<std::mutex> lock(data_mutex);
    queue_names.push_back(publisher_name);
}

void SimpleStage::push(BufferPtr data, std::string publisher_name)
{
    std::lock_guard<std::mutex> lock(data_mutex);
    pushed_data.push_back({data, publisher_name});
}

// ============================================================================
// StageTest
// ============================================================================

void StageTest::SetUp()
{
}
void StageTest::TearDown()
{
}

// ============================================================================
// TestThreadedStage
// ============================================================================

TestThreadedStage::TestThreadedStage(std::string name, size_t queue_size, bool leaky, bool trace_processing)
    : ThreadedStage(name, queue_size, leaky, trace_processing)
{
}

AppStatus TestThreadedStage::init()
{
    init_call_count++;
    return init_return_status;
}

AppStatus TestThreadedStage::deinit()
{
    deinit_call_count++;
    return deinit_return_status;
}

AppStatus TestThreadedStage::process(BufferPtr buffer)
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

int TestThreadedStage::get_init_call_count()
{
    return init_call_count.load();
}

int TestThreadedStage::get_deinit_call_count()
{
    return deinit_call_count.load();
}

int TestThreadedStage::get_process_call_count()
{
    return process_call_count.load();
}

// ============================================================================
// ThreadedStageTest
// ============================================================================

void ThreadedStageTest::SetUp()
{
}
void ThreadedStageTest::TearDown()
{
}

BufferPtr ThreadedStageTest::create_test_buffer()
{
    return std::make_shared<Buffer>(mock_buffer);
}

// ============================================================================
// PipelineTest
// ============================================================================

void PipelineTest::SetUp()
{
}
void PipelineTest::TearDown()
{
}

BufferPtr PipelineTest::create_test_buffer()
{
    return std::make_shared<Buffer>(mock_buffer);
}

std::shared_ptr<TestThreadedStage> PipelineTest::create_test_stage(std::string name, size_t queue_size)
{
    return std::make_shared<TestThreadedStage>(name, queue_size);
}

std::shared_ptr<Pipeline> PipelineTest::create_pipeline(std::string name)
{
    return std::make_shared<Pipeline>(name);
}
