#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>
#include <iostream>
#include <fstream>
#include <map>

#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "core_tests/core_tests_common.hpp"
#include "media_library/media_library.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/buffer_pool.hpp"

using namespace hailo_analytics::pipeline;
using namespace hailo_analytics::pipeline::codecs;
using namespace hailo_analytics::pipeline::sources;
using ::testing::_;
using ::testing::Return;

// MediaLibrary config deployed to device
static const std::string MEDIALIB_CONFIG_PATH =
    "/etc/imaging/cfg/medialib_configs/case_studies/single_stream_medialib_config.json";

// Helper to read config from file
static tl::expected<std::string, media_library_return> read_config_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        return tl::unexpected(media_library_return::MEDIA_LIBRARY_ERROR);
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return content;
}

// Test fixture for EncoderStage tests
class EncoderStageTest : public ::testing::Test
{
  protected:
    static constexpr uint32_t BUFFER_WIDTH = 3840; // 4K as in config
    static constexpr uint32_t BUFFER_HEIGHT = 2160;
    static constexpr uint32_t POOL_SIZE = 4;

    MediaLibraryBufferPoolPtr buffer_pool;
    HailoMediaLibraryBufferPtr real_buffer;
    MediaLibraryPtr media_library;
    std::string first_stream_id;

    void SetUp() override
    {
        // Create a buffer pool with real DMA buffers (required for encoder)
        // Uses 4K resolution to match the encoder config
        buffer_pool = std::make_shared<MediaLibraryBufferPool>(
            BUFFER_WIDTH, BUFFER_HEIGHT, HAILO_FORMAT_NV12, POOL_SIZE, HAILO_MEMORY_TYPE_DMABUF, "encoder_test_pool");

        auto status = buffer_pool->init();
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Failed to initialize buffer pool for encoder tests. "
                         << "Tests require DMA buffer allocation.";
        }

        // Acquire a buffer from the pool for use in tests
        real_buffer = std::make_shared<hailo_media_library_buffer>();
        status = buffer_pool->acquire_buffer(real_buffer);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Failed to acquire buffer from pool for encoder tests.";
        }

        // Create and initialize MediaLibrary
        auto config_result = read_config_file(MEDIALIB_CONFIG_PATH);
        if (!config_result.has_value())
        {
            GTEST_SKIP() << "MediaLibrary config not found: " << MEDIALIB_CONFIG_PATH;
        }

        auto media_lib_result = MediaLibrary::create();
        if (!media_lib_result.has_value())
        {
            GTEST_SKIP() << "Failed to create MediaLibrary";
        }

        media_library = media_lib_result.value();
        if (media_library->initialize(config_result.value()) != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Failed to initialize MediaLibrary";
        }

        // Get the first output stream ID for encoder tests
        auto output_streams = media_library->get_frontend_output_streams();
        if (!output_streams.has_value() || output_streams.value().empty())
        {
            GTEST_SKIP() << "No output streams available from MediaLibrary";
        }
        first_stream_id = output_streams.value().front().id;
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
        media_library.reset();
    }
};

// Helper stage for metadata checking tests
class MetadataCheckingStage : public TestThreadedStage
{
  public:
    std::atomic<bool> found_size_metadata{false};

    MetadataCheckingStage(const std::string &name = "metadata_checker", size_t queue_size = 10)
        : TestThreadedStage(name, queue_size, false, true)
    {
    }

    AppStatus process(BufferPtr data) override
    {
        if (data && data->get_metadata_of_type(MetadataType::SIZE).size() > 0)
        {
            found_size_metadata = true;
        }
        else
        {
            found_size_metadata = false;
        }

        TestThreadedStage::process(data);

        return AppStatus::SUCCESS;
    }
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(EncoderStageTest, CanBeConstructedWithDefaultParameters)
{
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    EXPECT_NE(stage, nullptr);
}

TEST_F(EncoderStageTest, CanBeConstructedWithBuilder)
{
    auto stage = EncoderStageBuild::create()
                     .set_stage_name("test_encoder")
                     .set_queue_size_opt(10)
                     .set_leaky_opt(false)
                     .set_trace_opt(true)
                     .buildptr();

    EXPECT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "test_encoder");
}

TEST_F(EncoderStageTest, BuilderCanSetCustomQueueSize)
{
    auto stage = EncoderStageBuild::create().set_stage_name("test_encoder").set_queue_size_opt(20).buildptr();

    EXPECT_NE(stage, nullptr);
}

TEST_F(EncoderStageTest, BuilderCanSetLeakyQueue)
{
    auto stage = EncoderStageBuild::create().set_stage_name("test_encoder").set_leaky_opt(true).buildptr();

    EXPECT_NE(stage, nullptr);
}

TEST_F(EncoderStageTest, BuilderCanSetTracingEnabled)
{
    auto stage = EncoderStageBuild::create().set_stage_name("test_encoder").set_trace_opt(true).buildptr();

    EXPECT_NE(stage, nullptr);
}

TEST_F(EncoderStageTest, BuilderThrowsWhenNameNotSet)
{
    EXPECT_THROW({ auto stage = EncoderStageBuild::create().set_queue_size_opt(10).buildptr(); }, std::runtime_error);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(EncoderStageTest, CanConfigureWithMediaLibrary)
{
    // Create stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");

    // Configure encoder stage with media library
    auto result = stage->configure(media_library, first_stream_id);
    EXPECT_EQ(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CanReconfigureWithNewStreamId)
{
    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    auto result1 = stage->configure(media_library, first_stream_id);
    ASSERT_EQ(result1, AppStatus::SUCCESS);

    // Reconfigure with same media library (simulates stream switch)
    auto result2 = stage->configure(media_library, first_stream_id);
    EXPECT_EQ(result2, AppStatus::SUCCESS);
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(EncoderStageTest, CanInitializeAfterConfigure)
{
    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    auto configure_result = stage->configure(media_library, first_stream_id);
    ASSERT_EQ(configure_result, AppStatus::SUCCESS);

    // Start should call init() internally
    auto start_result = stage->start();
    EXPECT_EQ(start_result, AppStatus::SUCCESS);

    // Clean up
    stage->stop();
}

TEST_F(EncoderStageTest, InitFailsWithoutConfigure)
{
    auto stage = std::make_shared<EncoderStage>("test_encoder");

    // Try to start without calling configure
    auto result = stage->start();

    EXPECT_NE(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CanStopAfterInit)
{
    // Create, configure and start stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(media_library, first_stream_id);
    stage->start();

    // Stop should call deinit() internally
    auto result = stage->stop();
    EXPECT_EQ(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CanHandleNullBuffer)
{
    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(media_library, first_stream_id);
    stage->add_queue("source");
    stage->start();

    // Push null buffer (should be ignored gracefully)
    BufferPtr null_buffer = nullptr;
    stage->push(null_buffer, "source");

    // Wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Stop stage (should not crash)
    auto result = stage->stop();
    EXPECT_EQ(result, AppStatus::SUCCESS);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(EncoderStageTest, CanStartAndStopMultipleTimes)
{
    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(media_library, first_stream_id);

    // Start and stop multiple times
    for (int i = 0; i < 3; i++)
    {
        auto start_result = stage->start();
        EXPECT_EQ(start_result, AppStatus::SUCCESS);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto stop_result = stage->stop();
        EXPECT_EQ(stop_result, AppStatus::SUCCESS);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Reconfigure after stop to allow restart
        if (i < 2) // Don't reconfigure after the last iteration
        {
            auto reconfig_result = stage->configure(media_library, first_stream_id);
            EXPECT_EQ(reconfig_result, AppStatus::SUCCESS);
        }
    }
}

TEST_F(EncoderStageTest, StopWithoutStartIsIdempotent)
{
    // *Idempotent means that calling the method multiple times has the same effect as calling it once.
    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(media_library, first_stream_id);

    // Stop without start should not crash
    auto result = stage->stop();
    EXPECT_EQ(result, AppStatus::SUCCESS);
}
