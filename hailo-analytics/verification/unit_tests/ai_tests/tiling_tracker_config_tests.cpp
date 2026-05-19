#include <stddef.h>
#include <stdint.h>
#include <tl/expected.hpp>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <memory>
#include <optional>
#include <string_view>

#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/ai/detection_tracker_stage.hpp"
#include "test_tier.hpp"
#include "gtest/gtest.h"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace tiling = hailo_analytics::analytics::tiling;
using hailo_analytics::pipeline::ai::DetectionTrackerStageBuild;

// =============================================================================
// TrackerConfigTest — tracker_config_t struct tests
// =============================================================================

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(DefaultsDisabled))
{
    tiling::tracker_config_t config;

    EXPECT_FALSE(config.enabled.has_value());
    EXPECT_FALSE(config.stage_name.has_value());
    EXPECT_FALSE(config.queue_size.has_value());
    EXPECT_FALSE(config.leaky.has_value());
    EXPECT_FALSE(config.trace.has_value());
    EXPECT_FALSE(config.labels_map.has_value());
    EXPECT_FALSE(config.max_tracklets.has_value());
    EXPECT_FALSE(config.max_missed_frames.has_value());
    EXPECT_FALSE(config.min_confirmed_frames.has_value());
    EXPECT_FALSE(config.aging_threshold.has_value());
    EXPECT_FALSE(config.add_threshold.has_value());
    EXPECT_FALSE(config.association_threshold.has_value());
    EXPECT_FALSE(config.iou_weight.has_value());
    EXPECT_FALSE(config.class_aware_tracking.has_value());
    EXPECT_FALSE(config.enable_kalman_filter.has_value());
    EXPECT_FALSE(config.position_std_weight.has_value());
    EXPECT_FALSE(config.velocity_std_weight.has_value());
    EXPECT_FALSE(config.smoothing_alpha.has_value());
}

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(MergeFromEnablesTracker))
{
    tiling::tracker_config_t config;
    tiling::tracker_config_t overrides;
    overrides.enabled = true;

    config.merge_from(overrides);

    ASSERT_TRUE(config.enabled.has_value());
    EXPECT_TRUE(config.enabled.value());
}

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(MergeFromDisablesTracker))
{
    tiling::tracker_config_t config;
    config.enabled = true;

    tiling::tracker_config_t overrides;
    overrides.enabled = false;

    config.merge_from(overrides);

    ASSERT_TRUE(config.enabled.has_value());
    EXPECT_FALSE(config.enabled.value());
}

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(MergeFromPartialOverride))
{
    tiling::tracker_config_t config;
    tiling::tracker_config_t overrides;
    overrides.max_missed_frames = 5;
    overrides.smoothing_alpha = 0.8f;

    config.merge_from(overrides);

    ASSERT_TRUE(config.max_missed_frames.has_value());
    EXPECT_EQ(config.max_missed_frames.value(), 5);
    ASSERT_TRUE(config.smoothing_alpha.has_value());
    EXPECT_FLOAT_EQ(config.smoothing_alpha.value(), 0.8f);

    // Non-overridden fields remain nullopt
    EXPECT_FALSE(config.stage_name.has_value());
    EXPECT_FALSE(config.queue_size.has_value());
    EXPECT_FALSE(config.max_tracklets.has_value());
    EXPECT_FALSE(config.iou_weight.has_value());
}

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(MergeFromEmptyPreservesEnabled))
{
    tiling::tracker_config_t config;
    config.enabled = true;
    config.max_missed_frames = 10;

    tiling::tracker_config_t empty;
    config.merge_from(empty);

    // Nullopt enabled does not overwrite
    ASSERT_TRUE(config.enabled.has_value());
    EXPECT_TRUE(config.enabled.value());
    // Optional fields are not overwritten by nullopt
    ASSERT_TRUE(config.max_missed_frames.has_value());
    EXPECT_EQ(config.max_missed_frames.value(), 10);
}

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(MergeFromZeroValuedOptionals))
{
    tiling::tracker_config_t config;
    tiling::tracker_config_t overrides;
    overrides.add_threshold = 0.0f;
    overrides.smoothing_alpha = 0.0f;

    config.merge_from(overrides);

    // Zero-valued optionals should still be merged (has_value() is true)
    ASSERT_TRUE(config.add_threshold.has_value());
    EXPECT_FLOAT_EQ(config.add_threshold.value(), 0.0f);
    ASSERT_TRUE(config.smoothing_alpha.has_value());
    EXPECT_FLOAT_EQ(config.smoothing_alpha.value(), 0.0f);
}

TEST(TrackerConfigTest, PR_NIGHTLY_TIER(ApplyToBuilder))
{
    tiling::tracker_config_t config;
    config.enabled = true;
    config.stage_name = "test_tracker";
    config.queue_size = 3;
    config.leaky = true;
    config.max_missed_frames = 5;
    config.smoothing_alpha = 0.6f;
    config.labels_map = std::map<uint8_t, std::string>{{0, "person"}, {1, "car"}};

    auto builder = DetectionTrackerStageBuild::create();
    config.apply_to(builder);
    auto stage = builder.buildptr();

    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "test_tracker");
}

// =============================================================================
// TilingDetectionConfigTrackerTest — composite config tests
// =============================================================================

TEST(TilingDetectionConfigTrackerTest, PR_NIGHTLY_TIER(BaseConfigTrackerDisabledByDefault))
{
    auto config = tiling::base_config();

    EXPECT_FALSE(config.tracker_config.enabled.has_value());
    EXPECT_FALSE(config.tracker_config.stage_name.has_value());
    EXPECT_FALSE(config.tracker_config.queue_size.has_value());
    EXPECT_FALSE(config.tracker_config.max_missed_frames.has_value());
}

TEST(TilingDetectionConfigTrackerTest, PR_NIGHTLY_TIER(MergeFromWithTrackerConfig))
{
    auto config = tiling::base_config();

    tiling::tiling_detection_config_t overrides;
    overrides.tracker_config.enabled = true;
    overrides.tracker_config.max_missed_frames = 7;

    config.merge_from(overrides);

    EXPECT_TRUE(config.tracker_config.enabled);
    ASSERT_TRUE(config.tracker_config.max_missed_frames.has_value());
    EXPECT_EQ(config.tracker_config.max_missed_frames.value(), 7);

    // Tiling config unchanged
    ASSERT_TRUE(config.tiling_config.stage_name.has_value());
    EXPECT_EQ(config.tiling_config.stage_name.value(), std::string(tiling::TILING_STAGE));
}

TEST(TilingDetectionConfigTrackerTest, PR_NIGHTLY_TIER(MergeFromEmptyPreservesTrackerDefaults))
{
    auto config = tiling::base_config();
    tiling::tiling_detection_config_t empty;

    config.merge_from(empty);

    EXPECT_FALSE(config.tracker_config.enabled.has_value());
    EXPECT_FALSE(config.tracker_config.stage_name.has_value());
}

// =============================================================================
// TilingDetectionPipelineTrackerTest — pipeline generation tests (device only)
// =============================================================================

class TilingDetectionPipelineTrackerTest : public ::testing::Test
{
};

TEST_F(TilingDetectionPipelineTrackerTest, PR_NIGHTLY_TIER(GenerateWithTrackerDisabled))
{
    auto result = tiling::generate_tiling_detection_pipeline("test_pipeline");
    ASSERT_TRUE(result.has_value()) << "Pipeline generation failed";

    auto pipeline = result.value();
    EXPECT_EQ(pipeline->get_name(), "test_pipeline");

    // Output stage should be the aggregator (no tracker)
    auto out_stage = pipeline->get_out_stage();
    ASSERT_NE(out_stage, nullptr);
    EXPECT_EQ(out_stage->get_name(), std::string(tiling::TILING_AGGREGATOR_STAGE));
}

TEST_F(TilingDetectionPipelineTrackerTest, PR_NIGHTLY_TIER(GenerateWithTrackerEnabled))
{
    tiling::tiling_detection_config_t cfg;
    cfg.tracker_config.enabled = true;

    auto result = tiling::generate_tiling_detection_pipeline("test_pipeline_tracked", cfg);
    ASSERT_TRUE(result.has_value()) << "Pipeline generation failed";

    auto pipeline = result.value();

    // Output stage should be the tracker
    auto out_stage = pipeline->get_out_stage();
    ASSERT_NE(out_stage, nullptr);
    EXPECT_EQ(out_stage->get_name(), std::string(tiling::TRACKER_STAGE));

    // Input stage should still be tiling
    auto in_stage = pipeline->get_in_stage();
    ASSERT_NE(in_stage, nullptr);
    EXPECT_EQ(in_stage->get_name(), std::string(tiling::TILING_STAGE));
}

TEST_F(TilingDetectionPipelineTrackerTest, PR_NIGHTLY_TIER(GenerateWithCustomTrackerStageName))
{
    tiling::tiling_detection_config_t cfg;
    cfg.tracker_config.enabled = true;
    cfg.tracker_config.stage_name = "my_custom_tracker";

    auto result = tiling::generate_tiling_detection_pipeline("test_pipeline_custom", cfg);
    ASSERT_TRUE(result.has_value()) << "Pipeline generation failed";

    auto pipeline = result.value();
    auto out_stage = pipeline->get_out_stage();
    ASSERT_NE(out_stage, nullptr);
    EXPECT_EQ(out_stage->get_name(), "my_custom_tracker");
}
