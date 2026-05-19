#include <stddef.h>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "media_library/buffer_pool.hpp"
#include "gtest/gtest.h"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"

using namespace hailo_analytics::pipeline;
using namespace hailo_analytics::pipeline::sinks;

// ============================================================================
// Test Fixture
// ============================================================================

class UdpStageTest : public ::testing::Test
{
  protected:
    MediaLibraryBufferPoolPtr buffer_pool;
    HailoMediaLibraryBufferPtr real_buffer;

    void SetUp() override
    {
        // Create a buffer pool with a small test buffer (1920x1080 NV12 format)
        // This allocates real DMA buffers that can be used by the UDP stage
        buffer_pool = std::make_shared<MediaLibraryBufferPool>(1920, 1080, HAILO_FORMAT_NV12, 1,
                                                               HAILO_MEMORY_TYPE_DMABUF, "udp_test_pool");

        auto status = buffer_pool->init();
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Failed to initialize buffer pool for UDP tests. "
                         << "Tests require DMA buffer allocation.";
        }

        // Acquire a buffer from the pool for use in tests
        real_buffer = std::make_shared<hailo_media_library_buffer>();
        status = buffer_pool->acquire_buffer(real_buffer);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Failed to acquire buffer from pool for UDP tests.";
        }
    }

    void TearDown() override
    {
        // Release the buffer back to the pool
        if (real_buffer && buffer_pool)
        {
            buffer_pool->release_buffer(real_buffer);
        }
        real_buffer.reset();

        // Free the buffer pool
        if (buffer_pool)
        {
            buffer_pool->free(false); // Don't fail if buffers still in use
        }
        buffer_pool.reset();
    }

    BufferPtr create_test_buffer_with_size(size_t size)
    {
        // Use the real buffer from the pool
        auto buffer = std::make_shared<Buffer>(real_buffer);
        auto size_metadata = std::make_shared<SizeMetadata>("test_label", size);
        buffer->add_metadata(size_metadata);
        return buffer;
    }

    BufferPtr create_test_buffer_without_size()
    {
        // Use the real buffer but without size metadata
        return std::make_shared<Buffer>(real_buffer);
    }
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(UdpStageTest, ConstructorCreatesValidStage)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "test_udp");
}

TEST_F(UdpStageTest, ConstructorWithAllParameters)
{
    auto stage = std::make_shared<UdpStage>("test_udp", 5, false, true, true);
    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "test_udp");
}

TEST_F(UdpStageTest, ConstructorWithCustomQueueSize)
{
    auto stage = std::make_shared<UdpStage>("test_udp", 10);
    ASSERT_NE(stage, nullptr);
}

TEST_F(UdpStageTest, ConstructorWithLeakyQueue)
{
    auto stage = std::make_shared<UdpStage>("test_udp", 5, true);
    ASSERT_NE(stage, nullptr);
}

// ============================================================================
// Builder Pattern Tests
// ============================================================================

TEST_F(UdpStageTest, BuilderCreatesStageWithCorrectName)
{
    auto stage = UdpStageBuild::create().set_stage_name("test_udp_builder").buildptr();

    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "test_udp_builder");
}

TEST_F(UdpStageTest, BuilderWithAllOptions)
{
    auto stage = UdpStageBuild::create()
                     .set_stage_name("full_config_udp")
                     .set_queue_size_opt(20)
                     .set_leaky_opt(true)
                     .set_printfps_opt(true)
                     .buildptr();

    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "full_config_udp");
}

TEST_F(UdpStageTest, BuilderWithQueueSizeOption)
{
    auto stage = UdpStageBuild::create().set_stage_name("test_udp").set_queue_size_opt(15).buildptr();

    ASSERT_NE(stage, nullptr);
}

TEST_F(UdpStageTest, BuilderWithLeakyOption)
{
    auto stage = UdpStageBuild::create().set_stage_name("test_udp").set_leaky_opt(true).buildptr();

    ASSERT_NE(stage, nullptr);
}

TEST_F(UdpStageTest, BuilderWithPrintFpsOption)
{
    auto stage = UdpStageBuild::create().set_stage_name("test_udp").set_printfps_opt(true).buildptr();

    ASSERT_NE(stage, nullptr);
}

TEST_F(UdpStageTest, BuilderThrowsWhenNameNotSet)
{
    EXPECT_THROW({ auto stage = UdpStageBuild::create().set_queue_size_opt(10).buildptr(); }, std::runtime_error);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(UdpStageTest, CreateSucceedsWithValidParameters)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->create("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, CreateWithH265Encoding)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->create("127.0.0.1", "5001", EncodingType::H265);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, CreateWithDifferentHost)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->create("192.168.1.100", "5000", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, CreateWithDifferentPort)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->create("127.0.0.1", "8080", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, ConfigureSucceedsWithValidParameters)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->configure("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, ConfigureCanBeCalledMultipleTimes)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    auto status1 = stage->configure("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(status1, AppStatus::SUCCESS);

    auto status2 = stage->configure("127.0.0.1", "5001", EncodingType::H265);
    EXPECT_EQ(status2, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, ReconfigureChangesSettings)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    // First configuration
    stage->configure("127.0.0.1", "5000", EncodingType::H264);

    // Reconfigure with different settings
    auto status = stage->configure("192.168.1.1", "6000", EncodingType::H265);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(UdpStageTest, InitSucceedsAfterConfiguration)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);

    auto status = stage->init();
    EXPECT_EQ(status, AppStatus::SUCCESS);

    stage->deinit();
}

TEST_F(UdpStageTest, InitFailsWhenNotConfigured)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    auto status = stage->init();
    EXPECT_EQ(status, AppStatus::UNINITIALIZED);
}

TEST_F(UdpStageTest, DeinitSucceedsAfterInit)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->init();

    auto status = stage->deinit();
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, InitDeinitCanBeCalledMultipleTimes)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);

    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(stage->init(), AppStatus::SUCCESS);
        EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
    }
}

// ============================================================================
// Buffer Processing Tests
// ============================================================================
// Note: UdpStage is a ThreadedStage, so process() is called internally by the
// stage's thread. Tests should push buffers to the queue, not call process() directly.

TEST_F(UdpStageTest, StartWithoutConfiguration)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->add_queue("source");

    // Should return uninitialized error
    EXPECT_EQ(stage->start(), AppStatus::UNINITIALIZED);
}

TEST_F(UdpStageTest, PushBufferWithoutSizeMetadata)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    // Push buffer without size metadata - stage should handle error internally
    auto buffer = create_test_buffer_without_size();
    stage->push(buffer, "source");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, PushValidBuffer)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    auto buffer = create_test_buffer_with_size(1024);
    stage->push(buffer, "source");

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, PushMultipleBuffers)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    for (int i = 0; i < 5; i++)
    {
        auto buffer = create_test_buffer_with_size(1024 * (i + 1));
        stage->push(buffer, "source");
    }

    // Wait for all buffers to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, PushBuffersWithDifferentSizes)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    std::vector<size_t> sizes = {512, 1024, 2048, 4096, 8192};

    for (auto size : sizes)
    {
        auto buffer = create_test_buffer_with_size(size);
        stage->push(buffer, "source");
    }

    // Wait for all buffers to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(UdpStageTest, StartStopLifecycle)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    // Give thread time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, MultipleStartStopCycles)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

TEST_F(UdpStageTest, ProcessBuffersThroughPipeline)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    // Push some buffers through the stage
    for (int i = 0; i < 3; i++)
    {
        auto buffer = create_test_buffer_with_size(1024);
        stage->push(buffer, "source");
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

// ============================================================================
// Encoding Type Tests
// ============================================================================

TEST_F(UdpStageTest, H264EncodingConfiguration)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->configure("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, H265EncodingConfiguration)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    auto status = stage->configure("127.0.0.1", "5000", EncodingType::H265);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, SwitchBetweenEncodingTypes)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->add_queue("source");

    // Start with H264
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
    auto buffer1 = create_test_buffer_with_size(1024);
    stage->push(buffer1, "source");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);

    // Switch to H265
    stage->configure("127.0.0.1", "5000", EncodingType::H265);
    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
    auto buffer2 = create_test_buffer_with_size(1024);
    stage->push(buffer2, "source");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(UdpStageTest, AddQueueBeforeConfiguration)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    ASSERT_NO_THROW({ stage->add_queue("source"); });
}

TEST_F(UdpStageTest, MultipleQueues)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    stage->add_queue("source1");
    stage->add_queue("source2");
    stage->add_queue("source3");

    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, StageInPipeline)
{
    auto pipeline = std::make_shared<Pipeline>("test_pipeline");
    auto udp_stage = std::make_shared<UdpStage>("udp_sink");

    udp_stage->configure("127.0.0.1", "5000", EncodingType::H264);

    ASSERT_NO_THROW({ pipeline->add_stage(udp_stage); });
}

TEST_F(UdpStageTest, CompleteWorkflowWithBuffers)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    // Configure
    auto config_status = stage->configure("127.0.0.1", "5000", EncodingType::H264);
    ASSERT_EQ(config_status, AppStatus::SUCCESS);

    // Add queue
    stage->add_queue("source");

    // Start (calls init internally)
    auto start_status = stage->start();
    ASSERT_EQ(start_status, AppStatus::SUCCESS);

    // Push buffers to the stage's queue
    for (int i = 0; i < 3; i++)
    {
        auto buffer = create_test_buffer_with_size(1024);
        stage->push(buffer, "source");
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop (calls deinit internally)
    auto stop_status = stage->stop();
    EXPECT_EQ(stop_status, AppStatus::SUCCESS);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(UdpStageTest, PushZeroSizeBuffer)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    auto buffer = create_test_buffer_with_size(0);
    stage->push(buffer, "source");

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, PushLargeBuffer)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);

    // Large buffer (1MB)
    auto buffer = create_test_buffer_with_size(1024 * 1024);
    stage->push(buffer, "source");

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, ReconfigureAfterProcessing)
{
    auto stage = std::make_shared<UdpStage>("test_udp");
    stage->add_queue("source");

    // First configuration and processing
    stage->configure("127.0.0.1", "5000", EncodingType::H264);
    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
    auto buffer1 = create_test_buffer_with_size(1024);
    stage->push(buffer1, "source");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);

    // Reconfigure and process again
    stage->configure("127.0.0.1", "6000", EncodingType::H265);
    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
    auto buffer2 = create_test_buffer_with_size(2048);
    stage->push(buffer2, "source");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, ConfigureWithEmptyHost)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    // Empty host should still create (validation happens in GStreamer)
    auto status = stage->configure("", "5000", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}

TEST_F(UdpStageTest, ConfigureWithEmptyPort)
{
    auto stage = std::make_shared<UdpStage>("test_udp");

    // Empty port should still create (validation happens in GStreamer)
    auto status = stage->configure("127.0.0.1", "", EncodingType::H264);
    EXPECT_EQ(status, AppStatus::SUCCESS);
}
