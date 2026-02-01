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
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/buffer_pool.hpp"

using namespace hailo_analytics::pipeline;
using namespace hailo_analytics::pipeline::codecs;
using namespace hailo_analytics::pipeline::sources;
using ::testing::_;
using ::testing::Return;

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

    // Encoder config deployed to /usr/bin on target
    static const std::string ENCODER_CONFIG_PATH;

    MediaLibraryBufferPoolPtr buffer_pool;
    HailoMediaLibraryBufferPtr real_buffer;
    std::string encoder_config_json;

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

        // Read encoder config from deployed location
        auto config_result = read_config_file(ENCODER_CONFIG_PATH);
        ASSERT_TRUE(config_result.has_value()) << "Failed to read encoder config from: " << ENCODER_CONFIG_PATH;
        encoder_config_json = config_result.value();
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

    // Helper to create a MediaLibraryEncoder for a given stream
    tl::expected<MediaLibraryEncoderPtr, media_library_return> create_encoder(const std::string &stream_id = "enc_0")
    {
        auto encoder_result = MediaLibraryEncoder::create(stream_id);
        if (!encoder_result.has_value())
        {
            return encoder_result;
        }

        auto encoder = encoder_result.value();
        auto config_status = encoder->set_config(encoder_config_json);
        if (config_status != MEDIA_LIBRARY_SUCCESS)
        {
            return tl::unexpected(config_status);
        }

        return encoder;
    }
};

// Static member initialization
// Config files are deployed from hailo-media-library/api/examples/config_examples to /usr/bin
const std::string EncoderStageTest::ENCODER_CONFIG_PATH = "/usr/bin/frontend_encoder_sink0.json";

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

TEST_F(EncoderStageTest, CanCreateEncoderFromMediaLibraryEncoder)
{
    // Create encoder
    auto encoder_result = create_encoder();
    ASSERT_TRUE(encoder_result.has_value()) << "Failed to create MediaLibraryEncoder";
    auto encoder = encoder_result.value();

    // Create stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");

    // Create encoder stage from media library encoder
    auto result = stage->configure(encoder);
    EXPECT_EQ(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CreateFailsWithNullEncoder)
{
    auto stage = std::make_shared<EncoderStage>("test_encoder");

    MediaLibraryEncoderPtr null_encoder = nullptr;
    auto result = stage->configure(null_encoder);

    EXPECT_NE(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CanConfigureWithNewEncoder)
{
    // Create first encoder
    auto encoder1_result = create_encoder("enc_0");
    ASSERT_TRUE(encoder1_result.has_value());
    auto encoder1 = encoder1_result.value();

    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    auto result1 = stage->configure(encoder1);
    ASSERT_EQ(result1, AppStatus::SUCCESS);

    // Create second encoder
    auto encoder2_result = create_encoder("enc_1");
    ASSERT_TRUE(encoder2_result.has_value());
    auto encoder2 = encoder2_result.value();

    // Reconfigure with new encoder
    auto result2 = stage->configure(encoder2);
    EXPECT_EQ(result2, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, ConfigureFailsWithNullEncoder)
{
    auto stage = std::make_shared<EncoderStage>("test_encoder");

    MediaLibraryEncoderPtr null_encoder = nullptr;
    auto result = stage->configure(null_encoder);

    EXPECT_NE(result, AppStatus::SUCCESS);
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(EncoderStageTest, CanInitializeAfterCreate)
{
    // Create encoder
    auto encoder_result = create_encoder();
    ASSERT_TRUE(encoder_result.has_value());
    auto encoder = encoder_result.value();

    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    auto configure_result = stage->configure(encoder);
    ASSERT_EQ(configure_result, AppStatus::SUCCESS);

    // Start should call init() internally
    auto start_result = stage->start();
    EXPECT_EQ(start_result, AppStatus::SUCCESS);

    // Clean up
    stage->stop();
}

TEST_F(EncoderStageTest, InitFailsWithoutCreate)
{
    auto stage = std::make_shared<EncoderStage>("test_encoder");

    // Try to start without calling create
    auto result = stage->start();

    EXPECT_NE(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CanStopAfterInit)
{
    // Create encoder
    auto encoder_result = create_encoder();
    ASSERT_TRUE(encoder_result.has_value());
    auto encoder = encoder_result.value();

    // Create, configure and start stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(encoder);
    stage->start();

    // Stop should call deinit() internally
    auto result = stage->stop();
    EXPECT_EQ(result, AppStatus::SUCCESS);
}

TEST_F(EncoderStageTest, CanHandleNullBuffer)
{
    // Create encoder
    auto encoder_result = create_encoder();
    ASSERT_TRUE(encoder_result.has_value());
    auto encoder = encoder_result.value();

    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(encoder);
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
    // Create encoder
    auto encoder_result = create_encoder();
    ASSERT_TRUE(encoder_result.has_value());
    auto encoder = encoder_result.value();

    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(encoder);

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
            auto reconfig_result = stage->configure(encoder);
            EXPECT_EQ(reconfig_result, AppStatus::SUCCESS);
        }
    }
}

TEST_F(EncoderStageTest, StopWithoutStartIsIdempotent)
{
    // *Idempotent means that calling the method multiple times has the same effect as calling it once.
    // Create encoder
    auto encoder_result = create_encoder();
    ASSERT_TRUE(encoder_result.has_value());
    auto encoder = encoder_result.value();

    // Create and configure stage
    auto stage = std::make_shared<EncoderStage>("test_encoder");
    stage->configure(encoder);

    // Stop without start should not crash
    auto result = stage->stop();
    EXPECT_EQ(result, AppStatus::SUCCESS);
}
