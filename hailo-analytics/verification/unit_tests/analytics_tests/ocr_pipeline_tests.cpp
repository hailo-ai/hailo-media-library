#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

#include "hailo_analytics/analytics/license_plate_recognition.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/routing/callback_stage.hpp"
#include "media_library/dsp_utils.hpp"
#include "test_images/test_image_loader.hpp"
#include "core_tests/core_tests_common.hpp"

namespace lpr = hailo_analytics::analytics::license_plate_recognition;
using hailo_analytics::pipeline::AppStatus;
using hailo_analytics::pipeline::Buffer;
using hailo_analytics::pipeline::BufferPtr;
using hailo_analytics::pipeline::Pipeline;
using hailo_analytics::pipeline::PipelinePtr;
using hailo_analytics::pipeline::StagePoolMode;
using hailo_analytics::pipeline::routing::CallbackStage;

// ============================================================================
// OcrConfigTest — config tests (no fixture)
// ============================================================================

TEST(OcrConfigTest, BaseConfigAiStageDefaults)
{
    auto config = lpr::ocr_base_config();
    const auto &ai = config.ai_config;

    ASSERT_TRUE(ai.stage_name.has_value());
    EXPECT_EQ(ai.stage_name.value(), "ocr_stage");

    ASSERT_TRUE(ai.hef_path.has_value());
    EXPECT_EQ(ai.hef_path.value(),
              "/home/root/apps/license_plate_recognition/resources/paddle_ocr_v5_mobile_recognition.hef");

    ASSERT_TRUE(ai.queue_size.has_value());
    EXPECT_EQ(ai.queue_size.value(), 5u);

    ASSERT_TRUE(ai.output_pool_size.has_value());
    EXPECT_EQ(ai.output_pool_size.value(), 50);

    ASSERT_TRUE(ai.group_id.has_value());
    EXPECT_EQ(ai.group_id.value(), "device0");

    ASSERT_TRUE(ai.batch_size.has_value());
    EXPECT_EQ(ai.batch_size.value(), 1);

    ASSERT_TRUE(ai.job_limit.has_value());
    EXPECT_EQ(ai.job_limit.value(), 10u);

    ASSERT_TRUE(ai.scheduler_threshold.has_value());
    EXPECT_EQ(ai.scheduler_threshold.value(), 1);

    ASSERT_TRUE(ai.dynamic_threshold.has_value());
    EXPECT_FALSE(ai.dynamic_threshold.value());

    ASSERT_TRUE(ai.scheduler_timeout.has_value());
    EXPECT_EQ(ai.scheduler_timeout.value(), std::chrono::milliseconds(100));

    ASSERT_TRUE(ai.pool_mode.has_value());
    EXPECT_EQ(ai.pool_mode.value(), StagePoolMode::BLOCKING);

    ASSERT_TRUE(ai.trace.has_value());
    EXPECT_TRUE(ai.trace.value());
}

TEST(OcrConfigTest, BaseConfigPostStageDefaults)
{
    auto config = lpr::ocr_base_config();
    const auto &post = config.post_config;

    ASSERT_TRUE(post.stage_name.has_value());
    EXPECT_EQ(post.stage_name.value(), "ocr_post");

    ASSERT_TRUE(post.so_path.has_value());
    EXPECT_EQ(post.so_path.value(), "/usr/lib/hailo-post-processes/libocr_post.so");

    ASSERT_TRUE(post.queue_size.has_value());
    EXPECT_EQ(post.queue_size.value(), 5u);

    ASSERT_TRUE(post.leaky.has_value());
    EXPECT_FALSE(post.leaky.value());

    ASSERT_TRUE(post.trace.has_value());
    EXPECT_TRUE(post.trace.value());
}

TEST(OcrConfigTest, BaseConfigNmsFieldsUnset)
{
    auto config = lpr::ocr_base_config();
    EXPECT_FALSE(config.ai_config.nms_score_threshold.has_value());
    EXPECT_FALSE(config.ai_config.nms_max_accumulated_mask_size_multiplier.has_value());
}

TEST(OcrConfigTest, BaseConfigMatchesHeaderConstants)
{
    auto config = lpr::ocr_base_config();

    EXPECT_EQ(config.ai_config.stage_name.value(), lpr::OCR_STAGE);
    EXPECT_EQ(config.ai_config.hef_path.value(), lpr::OCR_BASE_HEF);
    EXPECT_EQ(config.ai_config.group_id.value(), lpr::OCR_GROUP_ID);

    EXPECT_EQ(config.post_config.stage_name.value(), lpr::OCR_POST_STAGE);
    EXPECT_EQ(config.post_config.so_path.value(), lpr::OCR_POST_SO);
}

TEST(OcrConfigTest, MergeFromOverridesAiConfig)
{
    auto config = lpr::ocr_base_config();
    lpr::ocr_config_t overrides;
    overrides.ai_config.queue_size = 20;
    overrides.ai_config.batch_size = 10;

    config.merge_from(overrides);

    // Overridden fields
    EXPECT_EQ(config.ai_config.queue_size.value(), 20u);
    EXPECT_EQ(config.ai_config.batch_size.value(), 10);

    // Non-overridden fields remain at defaults
    EXPECT_EQ(config.ai_config.stage_name.value(), "ocr_stage");
    EXPECT_EQ(config.ai_config.output_pool_size.value(), 50);
    EXPECT_EQ(config.ai_config.job_limit.value(), 10u);
    EXPECT_EQ(config.ai_config.scheduler_threshold.value(), 1);
    EXPECT_EQ(config.ai_config.pool_mode.value(), StagePoolMode::BLOCKING);
}

TEST(OcrConfigTest, MergeFromOverridesPostConfig)
{
    auto config = lpr::ocr_base_config();
    lpr::ocr_config_t overrides;
    overrides.post_config.queue_size = 15;
    overrides.post_config.leaky = true;

    config.merge_from(overrides);

    // Overridden fields
    EXPECT_EQ(config.post_config.queue_size.value(), 15u);
    EXPECT_TRUE(config.post_config.leaky.value());

    // Non-overridden fields remain at defaults
    EXPECT_EQ(config.post_config.stage_name.value(), "ocr_post");
    EXPECT_EQ(config.post_config.so_path.value(), std::string(lpr::OCR_POST_SO));
    EXPECT_TRUE(config.post_config.trace.value());
}

TEST(OcrConfigTest, MergeFromEmptyDoesNotChangeDefaults)
{
    auto config = lpr::ocr_base_config();
    lpr::ocr_config_t empty_overrides;

    config.merge_from(empty_overrides);

    // All AI defaults preserved
    EXPECT_EQ(config.ai_config.stage_name.value(), "ocr_stage");
    EXPECT_EQ(config.ai_config.queue_size.value(), 5u);
    EXPECT_EQ(config.ai_config.batch_size.value(), 1);
    EXPECT_EQ(config.ai_config.output_pool_size.value(), 50);
    EXPECT_EQ(config.ai_config.job_limit.value(), 10u);

    // All post defaults preserved
    EXPECT_EQ(config.post_config.stage_name.value(), "ocr_post");
    EXPECT_EQ(config.post_config.queue_size.value(), 5u);
    EXPECT_FALSE(config.post_config.leaky.value());
    EXPECT_TRUE(config.post_config.trace.value());
}

TEST(OcrConfigTest, MergeFromOverridesBothConfigs)
{
    auto config = lpr::ocr_base_config();
    lpr::ocr_config_t overrides;
    overrides.ai_config.queue_size = 30;
    overrides.ai_config.dynamic_threshold = true;
    overrides.post_config.leaky = true;
    overrides.post_config.trace = false;

    config.merge_from(overrides);

    // AI overrides applied
    EXPECT_EQ(config.ai_config.queue_size.value(), 30u);
    EXPECT_TRUE(config.ai_config.dynamic_threshold.value());

    // Post overrides applied
    EXPECT_TRUE(config.post_config.leaky.value());
    EXPECT_FALSE(config.post_config.trace.value());

    // Non-overridden fields still at defaults
    EXPECT_EQ(config.ai_config.stage_name.value(), "ocr_stage");
    EXPECT_EQ(config.ai_config.batch_size.value(), 1);
    EXPECT_EQ(config.post_config.queue_size.value(), 5u);
}

// ============================================================================
// OcrPipelineTest — pipeline generation tests (fixture with GTEST_SKIP safety)
// ============================================================================

class OcrPipelineTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto result = lpr::generate_ocr_pipeline();
        if (!result.has_value())
        {
            GTEST_SKIP() << "generate_ocr_pipeline() failed (likely missing HailoRT runtime), skipping pipeline tests";
        }
    }
};

TEST_F(OcrPipelineTest, GenerateWithDefaultsSucceeds)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(OcrPipelineTest, GeneratedPipelineHasCorrectDefaultName)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->get_name(), "ocr_pipeline");
}

TEST_F(OcrPipelineTest, GenerateWithCustomName)
{
    auto result = lpr::generate_ocr_pipeline("my_custom_ocr");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->get_name(), "my_custom_ocr");

    // Stages keep their default names regardless of pipeline name
    auto pipeline = result.value();
    EXPECT_NE(pipeline->get_stage_by_name("ocr_stage"), nullptr);
    EXPECT_NE(pipeline->get_stage_by_name("ocr_post"), nullptr);
}

TEST_F(OcrPipelineTest, PipelineContainsTwoStages)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->get_stages().size(), 2u);
}

TEST_F(OcrPipelineTest, PipelineContainsOcrStage)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value()->get_stage_by_name("ocr_stage"), nullptr);
}

TEST_F(OcrPipelineTest, PipelineContainsPostStage)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value()->get_stage_by_name("ocr_post"), nullptr);
}

TEST_F(OcrPipelineTest, InStageIsOcrStage)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    auto pipeline = result.value();
    ASSERT_NE(pipeline->get_in_stage(), nullptr);
    EXPECT_EQ(pipeline->get_in_stage()->get_name(), "ocr_stage");
}

TEST_F(OcrPipelineTest, OutStageIsPostStage)
{
    auto result = lpr::generate_ocr_pipeline();
    ASSERT_TRUE(result.has_value());
    auto pipeline = result.value();
    ASSERT_NE(pipeline->get_out_stage(), nullptr);
    EXPECT_EQ(pipeline->get_out_stage()->get_name(), "ocr_post");
}

TEST_F(OcrPipelineTest, GenerateWithCustomConfigOverridesStageNames)
{
    lpr::ocr_config_t custom_config;
    custom_config.ai_config.stage_name = "custom_ai";
    custom_config.post_config.stage_name = "custom_post";

    auto result = lpr::generate_ocr_pipeline("ocr_pipeline", custom_config);
    ASSERT_TRUE(result.has_value());
    auto pipeline = result.value();

    // Custom names present
    EXPECT_NE(pipeline->get_stage_by_name("custom_ai"), nullptr);
    EXPECT_NE(pipeline->get_stage_by_name("custom_post"), nullptr);

    // Default names absent
    EXPECT_EQ(pipeline->get_stage_by_name("ocr_stage"), nullptr);
    EXPECT_EQ(pipeline->get_stage_by_name("ocr_post"), nullptr);
}

TEST_F(OcrPipelineTest, GenerateWithPartialConfigMergesWithDefaults)
{
    lpr::ocr_config_t partial_config;
    partial_config.ai_config.batch_size = 99;

    auto result = lpr::generate_ocr_pipeline("ocr_pipeline", partial_config);
    ASSERT_TRUE(result.has_value());
    auto pipeline = result.value();

    // Pipeline still generates with two stages
    EXPECT_EQ(pipeline->get_stages().size(), 2u);
    EXPECT_NE(pipeline->get_stage_by_name("ocr_stage"), nullptr);
    EXPECT_NE(pipeline->get_stage_by_name("ocr_post"), nullptr);
}

TEST_F(OcrPipelineTest, GenerateWithNulloptUsesDefaults)
{
    auto result = lpr::generate_ocr_pipeline("ocr_pipeline", std::nullopt);
    ASSERT_TRUE(result.has_value());
    auto pipeline = result.value();

    EXPECT_EQ(pipeline->get_name(), "ocr_pipeline");
    EXPECT_EQ(pipeline->get_stages().size(), 2u);
    EXPECT_NE(pipeline->get_stage_by_name("ocr_stage"), nullptr);
    EXPECT_NE(pipeline->get_stage_by_name("ocr_post"), nullptr);
}

// ============================================================================
// OcrPipelineInjectionTest — inject real NV12 image buffers through the pipeline
// ============================================================================

static const std::vector<std::string> LP_IMAGES = {
    "APQ5.png", "PE3820.png", "YHI4HXR.png", "KHO5ZZK.png", "SM7080.png",
};

static const uint32_t LP_IMG_WIDTH = 320;
static const uint32_t LP_IMG_HEIGHT = 48;
static const size_t LP_POOL_SIZE = 10;

static const uint32_t RESIZED_IMG_WIDTH = 160;
static const uint32_t RESIZED_IMG_HEIGHT = 24;
static const size_t RESIZED_POOL_SIZE = 5;

class OcrPipelineInjectionTest : public ::testing::Test
{
  protected:
    PipelinePtr m_pipeline;
    std::shared_ptr<TestThreadedStage> m_output_counter;
    MediaLibraryBufferPoolPtr m_lp_pool;
    MediaLibraryBufferPoolPtr m_resized_pool;

    void SetUp() override
    {
        m_lp_pool = std::make_shared<MediaLibraryBufferPool>(LP_IMG_WIDTH, LP_IMG_HEIGHT, HAILO_FORMAT_NV12,
                                                             LP_POOL_SIZE, HAILO_MEMORY_TYPE_DMABUF, "test_lp_pool");
        auto pool_ret = m_lp_pool->init();
        if (pool_ret != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "LP buffer pool init failed (no DMA heap on host) — skipping injection tests";
        }

        m_resized_pool =
            std::make_shared<MediaLibraryBufferPool>(RESIZED_IMG_WIDTH, RESIZED_IMG_HEIGHT, HAILO_FORMAT_NV12,
                                                     RESIZED_POOL_SIZE, HAILO_MEMORY_TYPE_DMABUF, "test_resized_pool");
        pool_ret = m_resized_pool->init();
        if (pool_ret != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Resized buffer pool init failed — skipping injection tests";
        }

        auto result = lpr::generate_ocr_pipeline();
        if (!result.has_value())
        {
            GTEST_SKIP() << "generate_ocr_pipeline() failed — skipping injection tests";
        }
        m_pipeline = result.value();
        m_pipeline->add_queue("test_injector");

        m_output_counter = std::make_shared<TestThreadedStage>("output_counter", 10);
        m_pipeline->add_subscriber(m_output_counter);

        auto start_status = m_pipeline->start();
        if (start_status != AppStatus::SUCCESS)
        {
            m_pipeline = nullptr;
            GTEST_SKIP() << "Pipeline start() failed (missing runtime deps) — skipping injection tests";
        }
        m_output_counter->start();
    }

    void TearDown() override
    {
        if (m_pipeline)
        {
            m_output_counter->stop();
            m_pipeline->stop();
        }
    }
};

TEST_F(OcrPipelineInjectionTest, InjectSingleImage)
{
    std::string path = get_test_image_path("APQ5.png");
    auto test_img = TestImageBuffer::load_from_file(m_lp_pool, path);
    if (!test_img.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    auto buf = std::make_shared<Buffer>(test_img.get());
    m_pipeline->push(buf, "test_injector");

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_EQ(m_output_counter->get_process_call_count(), 1);
}

TEST_F(OcrPipelineInjectionTest, InjectAllLicensePlateImages)
{
    for (const auto &name : LP_IMAGES)
    {
        std::string path = get_test_image_path(name);
        auto test_img = TestImageBuffer::load_from_file(m_lp_pool, path);
        if (!test_img.is_valid())
        {
            GTEST_SKIP() << "Test image not found at " << path;
        }
        auto buf = std::make_shared<Buffer>(test_img.get());
        m_pipeline->push(buf, "test_injector");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    EXPECT_EQ(m_output_counter->get_process_call_count(), static_cast<int>(LP_IMAGES.size()));
}

TEST_F(OcrPipelineInjectionTest, InjectMultipleCopiesOfSameImage)
{
    std::string path = get_test_image_path("APQ5.png");
    auto test_img = TestImageBuffer::load_from_file(m_lp_pool, path);
    if (!test_img.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    constexpr int NUM_COPIES = 3;
    for (int i = 0; i < NUM_COPIES; ++i)
    {
        auto buf = std::make_shared<Buffer>(test_img.get());
        m_pipeline->push(buf, "test_injector");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    EXPECT_EQ(m_output_counter->get_process_call_count(), NUM_COPIES);
}

TEST_F(OcrPipelineInjectionTest, InjectResizedImage)
{
    std::string path = get_test_image_path("APQ5.png");
    auto test_img = TestImageBuffer::load_from_file(m_resized_pool, path);
    if (!test_img.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    auto buf = std::make_shared<Buffer>(test_img.get());
    m_pipeline->push(buf, "test_injector");

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_EQ(m_output_counter->get_process_call_count(), 0);
}

TEST_F(OcrPipelineInjectionTest, PipelineStopsCleanlyAfterInjection)
{
    std::string path = get_test_image_path("APQ5.png");
    auto test_img = TestImageBuffer::load_from_file(m_lp_pool, path);
    if (!test_img.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    auto buf = std::make_shared<Buffer>(test_img.get());
    m_pipeline->push(buf, "test_injector");

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    m_output_counter->stop();
    EXPECT_EQ(m_pipeline->stop(), AppStatus::SUCCESS);
    m_pipeline = nullptr; // Prevent TearDown from stopping again
}

TEST_F(OcrPipelineInjectionTest, InjectImageAndVerifyNoMetadataLoss)
{
    std::string path = get_test_image_path("APQ5.png");
    auto test_img = TestImageBuffer::load_from_file(m_lp_pool, path);
    if (!test_img.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    auto buf = std::make_shared<Buffer>(test_img.get());
    EXPECT_NE(buf->get_roi(), nullptr) << "Buffer ROI should be non-null before injection";

    m_pipeline->push(buf, "test_injector");

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_GE(m_output_counter->get_process_call_count(), 1);
}

// ============================================================================
// OcrLetterboxPostprocessTest — validate OCR on unsized images via DSP letterbox
// ============================================================================

struct LpUnsizedTestCase
{
    const char *filename;
    const char *expected_15h;
    const char *expected_15l;
};

// TODO: These plates produce incorrect high-confidence results on 15L.
// Remove entries as the OCR model is fixed.
static const std::unordered_set<std::string> KNOWN_15L_FAILURES = {
    "ADG5805.png", // expected "ADG5805", got "0G5805"
    "NJCJ827.png", // expected "JCJ82T", got "JCJ827"
    "U23756.png",  // expected "U23756", got "23756"
    "XGWB298.png", // expected "RGWB298", got "GWB298"
    "YFF-0OZ.png", // expected "YFF-0OZ", got "YFFOOZ"
    "YG55UHM.png", // expected "YG55UHM", got "YC55UHM"
    "YKO5CDU.png", // expected "YKO5CDU", got "YO5CDU"
};

static const LpUnsizedTestCase LP_UNSIZED_TEST_CASES[] = {
    {"167 342-28.png", "67:342-28", "6734228"},
    {"318.png", "318", "318"},
    {"439-EBB.png", "439-EBB", "439EBB"},
    {"83162-68.png", "8316268", "8316268"},
    {"A28D89.png", "A28D89", "A28D89"},
    {"ADG5805.png", "AD65805", "ADG5805"},
    {"AK52ML.png", "AK52ML", "AK52ML"},
    {"AOR148.png", "AOR148", "AOR148"},
    {"AV63LBO.png", "AV63LBO", "AV63LBO"},
    {"BIKABPK9EL.png", "KAPK981", "KAPK981"},
    {"BKHT-14E.png", "3KHT-14E", "KHT-14E"},
    {"CD-6869.png", "CD68", "CD6869"},
    {"EP851FP.png", "EP851FP", "EP851FP"},
    {"GM6UXS.png", "GMGUXS", "GMGUXSI"},
    {"I742226C.png", "1742260", "74226C"},
    {"KBR-0375.png", "KBR-0375", "KBR-0375"},
    {"LL851.png", "LL851", "LL851"},
    {"LN68FDD.png", "LN68FDD", "LN68FDD"},
    {"MATZ439.png", "ATZ439", "ATZ439"},
    {"MDB10BV.png", "MD10BV", "MD10BV"},
    {"ME05008.png", "ME05008", "E05008"},
    {"MXIGDKE.png", "MXI6DKE", "MXIGDKE"},
    {"NA52BUU.png", "NA52BUU", "NA52BUU"},
    {"ND.13180.png", "NOBEO", "O1BEO"},
    {"NJCJ827.png", "IJCJ827", "JCJ82T"},
    {"NLT093.png", "NLT093", "NLTO93"},
    {"PJ62VXG.png", "PJ62VXG", "PJ62VXG"},
    {"SA883K.png", "SA883K", "SAG3K"},
    {"SN55JVK.png", "SNS5JVK", "SN5JVK"},
    {"TV89303.png", "TV89303", "TV89303"},
    {"U23756.png", "U23756", "U23756"},
    {"VD5262200.png", "VDI526200", "WD2600"},
    {"W567JVW.png", "Y56J", "R567JNN"},
    {"XGWB298.png", "GWB298", "RGWB298"},
    {"YFF-0OZ.png", "YFF-0OZ", "YFF-0OZ"},
    {"YG55UHM.png", "YG55UHM", "YG55UHM"},
    {"YKO5CDU.png", "YKOSCDU", "YKO5CDU"},
};

class OcrLetterboxPostprocessTest : public ::testing::Test
{
  protected:
    PipelinePtr m_pipeline;
    std::shared_ptr<CallbackStage> m_callback_stage;
    MediaLibraryBufferPoolPtr m_lp_pool;

    std::mutex m_output_mutex;
    std::vector<BufferPtr> m_output_buffers;

    void SetUp() override
    {
        dsp_status dsp_ret = dsp_utils::acquire_device();
        if (dsp_ret != DSP_SUCCESS)
        {
            GTEST_SKIP() << "DSP acquire_device() failed — skipping letterbox tests";
        }

        m_lp_pool = std::make_shared<MediaLibraryBufferPool>(LP_IMG_WIDTH, LP_IMG_HEIGHT, HAILO_FORMAT_NV12,
                                                             LP_POOL_SIZE, HAILO_MEMORY_TYPE_DMABUF, "test_lp_pool");
        auto pool_ret = m_lp_pool->init();
        if (pool_ret != MEDIA_LIBRARY_SUCCESS)
        {
            dsp_utils::release_device();
            GTEST_SKIP() << "LP buffer pool init failed — skipping letterbox tests";
        }

        auto result = lpr::generate_ocr_pipeline();
        if (!result.has_value())
        {
            dsp_utils::release_device();
            GTEST_SKIP() << "generate_ocr_pipeline() failed — skipping letterbox tests";
        }
        m_pipeline = result.value();
        m_pipeline->add_queue("test_injector");

        m_callback_stage = std::make_shared<CallbackStage>("test_callback", 10, false, [this](BufferPtr buf) {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_output_buffers.push_back(buf);
        });
        m_pipeline->add_subscriber(m_callback_stage);

        auto start_status = m_pipeline->start();
        if (start_status != AppStatus::SUCCESS)
        {
            m_pipeline = nullptr;
            dsp_utils::release_device();
            GTEST_SKIP() << "Pipeline start() failed — skipping letterbox tests";
        }
        m_callback_stage->start();
    }

    void TearDown() override
    {
        if (m_pipeline)
        {
            m_callback_stage->stop();
            m_pipeline->stop();
        }
        dsp_utils::release_device();
    }
};

TEST_F(OcrLetterboxPostprocessTest, PostprocessOutputMatchesUnsizedLicensePlate)
{
    for (const auto &tc : LP_UNSIZED_TEST_CASES)
    {
        std::string name(tc.filename);
        SCOPED_TRACE("Image: " + name);

#if defined(HAILO_PLATFORM_15L)
        if (KNOWN_15L_FAILURES.count(name))
        {
            continue;
        }
#endif

#if defined(HAILO_PLATFORM_15H)
        std::string expected(tc.expected_15h);
#elif defined(HAILO_PLATFORM_15L)
        std::string expected(tc.expected_15l);
#else
#error "Platform not defined: expected HAILO_PLATFORM_15H or HAILO_PLATFORM_15L"
#endif

        std::string path = get_test_image_path_unsized(name);
        auto test_img = TestImageBuffer::load_from_file_letterbox(m_lp_pool, path);
        if (!test_img.is_valid())
        {
            GTEST_SKIP() << "Test image not found or letterbox resize failed at " << path;
        }

        {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_output_buffers.clear();
        }

        auto buf = std::make_shared<Buffer>(test_img.get());
        m_pipeline->push(buf, "test_injector");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::lock_guard<std::mutex> lock(m_output_mutex);
        ASSERT_EQ(m_output_buffers.size(), 1u) << "Expected exactly one output buffer for " << name;

        auto roi = m_output_buffers[0]->get_roi();
        ASSERT_NE(roi, nullptr);

        auto objects = roi->get_objects_typed(HAILO_CLASSIFICATION);
        std::vector<HailoClassificationPtr> ocr_classifications;
        for (const auto &obj : objects)
        {
            auto cls = std::dynamic_pointer_cast<HailoClassification>(obj);
            if (cls && cls->get_classification_type() == "ocr")
            {
                ocr_classifications.push_back(cls);
            }
        }

        ASSERT_GE(ocr_classifications.size(), 1u) << "No OCR classification found for " << name;

        auto &best = ocr_classifications[0];
        std::string label = best->get_label();
        float confidence = best->get_confidence();

        if (confidence > 0.9f)
        {
            EXPECT_EQ(label, expected) << "High-confidence prediction mismatch for " << name;
        }
        if (label != expected)
        {
            EXPECT_LT(confidence, 0.9f) << "Wrong label '" << label << "' has high confidence for " << name;
        }
    }
}
