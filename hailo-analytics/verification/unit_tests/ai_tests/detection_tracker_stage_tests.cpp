#include <hailopp/hailotracker.h>
#include <hailort.h>
#include <stdint.h>
#include <hailo_postprocess_tools/objects/hailo_common.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/buffer_pool.hpp>
#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>

#include "media_library/cloexec_fstream.hpp"
#include "hailo_analytics/pipeline/ai/detection_tracker_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "gtest/gtest.h"

using namespace hailo_analytics::pipeline;
using namespace hailo_analytics::pipeline::ai;

// =============================================================================
// Test subclass exposing protected methods for direct testing
// =============================================================================

class TestableDetectionTrackerStage : public DetectionTrackerStage
{
  public:
    using DetectionTrackerStage::DetectionTrackerStage;

    using DetectionTrackerStage::hailo_detections_to_hailort_detections;
    using DetectionTrackerStage::m_mot_output_path;
    using DetectionTrackerStage::m_save_mot_output;
    using DetectionTrackerStage::tracklets_to_hailo_detections;
};

// =============================================================================
// Helpers
// =============================================================================

static BufferPtr create_test_buffer()
{
    HailoMediaLibraryBufferPtr mock_buffer = nullptr;
    return std::make_shared<Buffer>(mock_buffer);
}

static BufferPtr create_test_buffer_with_tensor_metadata()
{
    auto buffer = create_test_buffer();
    auto tensor_buffer = create_test_buffer();
    auto tensor_metadata = std::make_shared<TensorMetadata>(tensor_buffer, "dummy_tensor");
    buffer->add_metadata(tensor_metadata);
    return buffer;
}

static void add_detection_to_roi(const HailoROIPtr &roi, float xmin, float ymin, float w, float h, int class_id,
                                 const std::string &label, float confidence)
{
    HailoBBox bbox(xmin, ymin, w, h);
    auto det = std::make_shared<HailoDetection>(bbox, class_id, label, confidence);
    roi->add_object(det);
}

static constexpr const char *MOT_TEST_FILE = "/tmp/detection_tracker_stage_test_mot.csv";

// =============================================================================
// Test Fixtures
// =============================================================================

class DetectionTrackerStageTest : public ::testing::Test
{
  protected:
    void TearDown() override
    {
        std::remove(MOT_TEST_FILE);
    }
};

// =============================================================================
// Builder Tests
// =============================================================================

TEST_F(DetectionTrackerStageTest, BuilderRequiresName)
{
    EXPECT_THROW(DetectionTrackerStageBuild::create().buildptr(), std::runtime_error);
}

TEST_F(DetectionTrackerStageTest, BuilderDefaults)
{
    auto stage = DetectionTrackerStageBuild::create().set_stage_name("test_tracker").buildptr();

    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "test_tracker");
}

TEST_F(DetectionTrackerStageTest, BuilderAllOptions)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}, {1, "car"}};

    auto stage = DetectionTrackerStageBuild::create()
                     .set_stage_name("full_tracker")
                     .set_queue_size_opt(10)
                     .set_leaky_opt(true)
                     .set_trace_opt(false)
                     .set_max_tracklets(50)
                     .set_max_missed_frames(5)
                     .set_min_confirmed_frames(2)
                     .set_aging_threshold(10)
                     .set_add_threshold(0.6f)
                     .set_association_threshold(0.4f)
                     .set_iou_weight(0.8f)
                     .set_class_aware_tracking(true)
                     .set_enable_kalman_filter(true)
                     .set_position_std_weight(0.05f)
                     .set_velocity_std_weight(0.1f)
                     .set_smoothing_alpha(0.7f)
                     .set_labels_map(labels)
                     .set_mot_output_path(MOT_TEST_FILE)
                     .buildptr();

    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_name(), "full_tracker");
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_F(DetectionTrackerStageTest, InitAndDeinit)
{
    auto stage = DetectionTrackerStageBuild::create().set_stage_name("lifecycle_tracker").buildptr();

    EXPECT_EQ(stage->init(), AppStatus::SUCCESS);
    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

TEST_F(DetectionTrackerStageTest, DoubleDeinitIsSafe)
{
    auto stage = DetectionTrackerStageBuild::create().set_stage_name("double_deinit").buildptr();

    EXPECT_EQ(stage->init(), AppStatus::SUCCESS);
    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

TEST_F(DetectionTrackerStageTest, StartAndStop)
{
    auto stage = DetectionTrackerStageBuild::create().set_stage_name("start_stop_tracker").buildptr();
    stage->add_queue("source");

    EXPECT_EQ(stage->start(), AppStatus::SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(stage->stop(), AppStatus::SUCCESS);
}

// =============================================================================
// Process Tests
// =============================================================================

TEST_F(DetectionTrackerStageTest, ProcessNoDetections)
{
    auto stage =
        DetectionTrackerStageBuild::create().set_stage_name("no_det_tracker").set_min_confirmed_frames(1).buildptr();

    ASSERT_EQ(stage->init(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);

    auto detections = hailo_common::get_hailo_detections(buffer->get_roi());
    EXPECT_EQ(detections.size(), 0u);

    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

TEST_F(DetectionTrackerStageTest, ProcessWithDetections)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}};
    auto stage = DetectionTrackerStageBuild::create()
                     .set_stage_name("det_tracker")
                     .set_min_confirmed_frames(1)
                     .set_labels_map(labels)
                     .buildptr();

    ASSERT_EQ(stage->init(), AppStatus::SUCCESS);

    // Feed the same detection multiple times to get it confirmed and tracked.
    // Coords are normalized [0, 1] because tracklets_to_hailo_detections clamps to that range and
    // now drops zero-extent bboxes — out-of-range inputs collapse to (1,1,0,0) and get filtered.
    for (int i = 0; i < 3; i++)
    {
        BufferPtr buffer = create_test_buffer_with_tensor_metadata();
        add_detection_to_roi(buffer->get_roi(), 0.10f, 0.10f, 0.05f, 0.05f, 0, "person", 0.9f);
        EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);
    }

    // After min_confirmed_frames=1, we should see tracked detections with unique IDs
    BufferPtr check_buffer = create_test_buffer_with_tensor_metadata();
    add_detection_to_roi(check_buffer->get_roi(), 0.10f, 0.10f, 0.05f, 0.05f, 0, "person", 0.9f);
    EXPECT_EQ(stage->process(check_buffer), AppStatus::SUCCESS);

    auto detections = hailo_common::get_hailo_detections(check_buffer->get_roi());
    EXPECT_GE(detections.size(), 1u);

    if (!detections.empty())
    {
        auto track_ids = hailo_common::get_hailo_track_id(detections[0]);
        EXPECT_GE(track_ids.size(), 1u);
    }

    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

TEST_F(DetectionTrackerStageTest, ProcessMultipleFramesConsistentIDs)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}};
    auto stage = DetectionTrackerStageBuild::create()
                     .set_stage_name("multi_frame_tracker")
                     .set_min_confirmed_frames(1)
                     .set_labels_map(labels)
                     .buildptr();

    ASSERT_EQ(stage->init(), AppStatus::SUCCESS);

    int first_track_id = -1;

    for (int frame = 0; frame < 5; frame++)
    {
        BufferPtr buffer = create_test_buffer_with_tensor_metadata();
        // Normalized [0, 1] — see ProcessWithDetections.
        add_detection_to_roi(buffer->get_roi(), 0.10f, 0.10f, 0.05f, 0.05f, 0, "person", 0.9f);
        EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);

        auto detections = hailo_common::get_hailo_detections(buffer->get_roi());
        if (!detections.empty())
        {
            auto track_ids = hailo_common::get_hailo_track_id(detections[0]);
            if (!track_ids.empty())
            {
                int current_id = track_ids[0]->get_id();
                if (first_track_id == -1)
                {
                    first_track_id = current_id;
                }
                else
                {
                    EXPECT_EQ(current_id, first_track_id)
                        << "Track ID changed between frames " << (frame - 1) << " and " << frame;
                }
            }
        }
    }

    EXPECT_NE(first_track_id, -1) << "Expected at least one tracked detection across frames";
    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

// =============================================================================
// MOT Output Tests
// =============================================================================

TEST_F(DetectionTrackerStageTest, MotOutputDisabledByDefault)
{
    auto stage = DetectionTrackerStageBuild::create().set_stage_name("no_mot_tracker").buildptr();
    ASSERT_EQ(stage->init(), AppStatus::SUCCESS);

    BufferPtr buffer = create_test_buffer();
    stage->process(buffer);

    EXPECT_FALSE(std::filesystem::exists(MOT_TEST_FILE));
    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

TEST_F(DetectionTrackerStageTest, MotOutputFlagsDefaultDisabled)
{
    hailo_tracker_config_t config = HAILO_TRACKER_CONFIG_DEFAULT;
    std::map<uint8_t, std::string> labels;
    TestableDetectionTrackerStage stage("default_ctor_tracker", config, labels);
    EXPECT_FALSE(stage.m_save_mot_output);
    EXPECT_TRUE(stage.m_mot_output_path.empty());
}

TEST_F(DetectionTrackerStageTest, MotOutputCreatesFile)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}};
    auto stage = DetectionTrackerStageBuild::create()
                     .set_stage_name("mot_tracker")
                     .set_min_confirmed_frames(1)
                     .set_labels_map(labels)
                     .set_mot_output_path(MOT_TEST_FILE)
                     .buildptr();

    ASSERT_EQ(stage->init(), AppStatus::SUCCESS);

    // Feed detections to produce tracked output (normalized [0, 1] — see ProcessWithDetections).
    for (int i = 0; i < 3; i++)
    {
        BufferPtr buffer = create_test_buffer_with_tensor_metadata();
        add_detection_to_roi(buffer->get_roi(), 0.10f, 0.20f, 0.05f, 0.06f, 0, "person", 0.85f);
        EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);
    }

    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);

    ASSERT_TRUE(std::filesystem::exists(MOT_TEST_FILE));

    cloexec::ifstream mot_file(MOT_TEST_FILE);
    ASSERT_TRUE(mot_file.is_open());

    std::string line;
    int line_count = 0;
    while (std::getline(mot_file, line))
    {
        EXPECT_FALSE(line.empty());
        line_count++;
    }
    EXPECT_GT(line_count, 0) << "MOT file should contain at least one tracking line";
}

TEST_F(DetectionTrackerStageTest, MotOutputFormat)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}};
    auto stage = DetectionTrackerStageBuild::create()
                     .set_stage_name("mot_format_tracker")
                     .set_min_confirmed_frames(1)
                     .set_labels_map(labels)
                     .set_mot_output_path(MOT_TEST_FILE)
                     .buildptr();

    ASSERT_EQ(stage->init(), AppStatus::SUCCESS);

    // Normalized [0, 1] — see ProcessWithDetections.
    for (int i = 0; i < 3; i++)
    {
        BufferPtr buffer = create_test_buffer_with_tensor_metadata();
        add_detection_to_roi(buffer->get_roi(), 0.10f, 0.20f, 0.30f, 0.40f, 0, "person", 0.95f);
        EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);
    }

    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);

    cloexec::ifstream mot_file(MOT_TEST_FILE);
    ASSERT_TRUE(mot_file.is_open());

    std::string line;
    while (std::getline(mot_file, line))
    {
        // Verify MOT format: <frame>,<id>,<bb_left>,<bb_top>,<bb_width>,<bb_height>,<conf>,<class>,<visibility>
        std::stringstream ss(line);
        std::string token;
        int field_count = 0;
        while (std::getline(ss, token, ','))
        {
            field_count++;
        }
        EXPECT_EQ(field_count, 9) << "MOT line should have exactly 9 comma-separated fields: " << line;

        // Verify last field is -1 (visibility placeholder)
        EXPECT_TRUE(line.length() >= 2);
        EXPECT_EQ(line.substr(line.length() - 2), "-1");
    }
}

// =============================================================================
// Data Conversion Tests (via TestableDetectionTrackerStage)
// =============================================================================

TEST_F(DetectionTrackerStageTest, HailoDetectionsToHailortDetections)
{
    HailoBBox bbox1(10.0f, 20.0f, 30.0f, 40.0f);
    auto det1 = std::make_shared<HailoDetection>(bbox1, 1, "car", 0.9f);

    HailoBBox bbox2(50.0f, 60.0f, 70.0f, 80.0f);
    auto det2 = std::make_shared<HailoDetection>(bbox2, 2, "person", 0.8f);

    std::vector<HailoDetectionPtr> detections = {det1, det2};

    auto buffer = TestableDetectionTrackerStage::hailo_detections_to_hailort_detections(detections);
    auto *tracker_dets = reinterpret_cast<hailo_detections_t *>(buffer.data());

    ASSERT_EQ(tracker_dets->count, 2);

    // First detection
    EXPECT_FLOAT_EQ(tracker_dets->detections[0].x_min, 10.0f);
    EXPECT_FLOAT_EQ(tracker_dets->detections[0].y_min, 20.0f);
    EXPECT_FLOAT_EQ(tracker_dets->detections[0].x_max, 40.0f); // xmin + width
    EXPECT_FLOAT_EQ(tracker_dets->detections[0].y_max, 60.0f); // ymin + height
    EXPECT_FLOAT_EQ(tracker_dets->detections[0].score, 0.9f);
    EXPECT_EQ(tracker_dets->detections[0].class_id, 1);

    // Second detection
    EXPECT_FLOAT_EQ(tracker_dets->detections[1].x_min, 50.0f);
    EXPECT_FLOAT_EQ(tracker_dets->detections[1].y_min, 60.0f);
    EXPECT_FLOAT_EQ(tracker_dets->detections[1].x_max, 120.0f); // xmin + width
    EXPECT_FLOAT_EQ(tracker_dets->detections[1].y_max, 140.0f); // ymin + height
    EXPECT_FLOAT_EQ(tracker_dets->detections[1].score, 0.8f);
    EXPECT_EQ(tracker_dets->detections[1].class_id, 2);
}

TEST_F(DetectionTrackerStageTest, HailoDetectionsToHailortDetectionsEmpty)
{
    std::vector<HailoDetectionPtr> empty;

    auto buffer = TestableDetectionTrackerStage::hailo_detections_to_hailort_detections(empty);
    auto *tracker_dets = reinterpret_cast<hailo_detections_t *>(buffer.data());

    EXPECT_EQ(tracker_dets->count, 0);
}

TEST_F(DetectionTrackerStageTest, TrackletsToHailoDetections)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}, {1, "car"}};
    hailo_tracker_config_t config = HAILO_TRACKER_CONFIG_DEFAULT;
    TestableDetectionTrackerStage stage("tracklet_test", config, labels);

    // Build tracklets manually. Coords are normalized [0, 1] because the production
    // code clamps to that range (Kalman drift guard).
    hailo_tracklet_t tracklet_arr[2];
    tracklet_arr[0].id = 42;
    tracklet_arr[0].detection = {0.20f, 0.10f, 0.80f, 0.60f, 0.9f, 0};
    tracklet_arr[0].state = HAILO_TRACKLET_STATE_TRACKED;
    tracklet_arr[0].frames_since_update = 0;
    tracklet_arr[0].total_frames_tracked = 5;
    tracklet_arr[0].velocity_x = 0.0f;
    tracklet_arr[0].velocity_y = 0.0f;

    tracklet_arr[1].id = 99;
    tracklet_arr[1].detection = {0.50f, 0.25f, 0.55f, 0.30f, 0.85f, 1};
    tracklet_arr[1].state = HAILO_TRACKLET_STATE_TRACKED;
    tracklet_arr[1].frames_since_update = 0;
    tracklet_arr[1].total_frames_tracked = 3;
    tracklet_arr[1].velocity_x = 0.0f;
    tracklet_arr[1].velocity_y = 0.0f;

    hailo_tracklets_t tracklets;
    tracklets.tracklets = tracklet_arr;
    tracklets.count = 2;

    auto result = stage.tracklets_to_hailo_detections(tracklets);
    ASSERT_EQ(result.size(), 2u);

    // First tracklet
    EXPECT_EQ(result[0]->get_label(), "person");
    EXPECT_FLOAT_EQ(result[0]->get_confidence(), 0.9f);
    EXPECT_EQ(result[0]->get_class_id(), 0);
    HailoBBox bbox0 = result[0]->get_bbox();
    EXPECT_FLOAT_EQ(bbox0.xmin(), 0.10f);
    EXPECT_FLOAT_EQ(bbox0.ymin(), 0.20f);
    EXPECT_FLOAT_EQ(bbox0.width(), 0.50f);  // x_max - x_min = 0.60 - 0.10
    EXPECT_FLOAT_EQ(bbox0.height(), 0.60f); // y_max - y_min = 0.80 - 0.20

    auto ids0 = hailo_common::get_hailo_track_id(result[0]);
    ASSERT_EQ(ids0.size(), 1u);
    EXPECT_EQ(ids0[0]->get_id(), 42);

    // Second tracklet
    EXPECT_EQ(result[1]->get_label(), "car");
    EXPECT_FLOAT_EQ(result[1]->get_confidence(), 0.85f);
    EXPECT_EQ(result[1]->get_class_id(), 1);

    auto ids1 = hailo_common::get_hailo_track_id(result[1]);
    ASSERT_EQ(ids1.size(), 1u);
    EXPECT_EQ(ids1[0]->get_id(), 99);
}

TEST_F(DetectionTrackerStageTest, TrackletsToHailoDetectionsFiltersNonTracked)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}};
    hailo_tracker_config_t config = HAILO_TRACKER_CONFIG_DEFAULT;
    TestableDetectionTrackerStage stage("filter_test", config, labels);

    // Normalized [0, 1] coords because tracklets_to_hailo_detections clamps to that range
    // and drops zero-extent bboxes.
    hailo_tracklet_t tracklet_arr[3];

    tracklet_arr[0].id = 1;
    tracklet_arr[0].detection = {0.10f, 0.20f, 0.30f, 0.40f, 0.9f, 0};
    tracklet_arr[0].state = HAILO_TRACKLET_STATE_NEW;
    tracklet_arr[0].frames_since_update = 0;
    tracklet_arr[0].total_frames_tracked = 1;
    tracklet_arr[0].velocity_x = 0.0f;
    tracklet_arr[0].velocity_y = 0.0f;

    tracklet_arr[1].id = 2;
    tracklet_arr[1].detection = {0.50f, 0.10f, 0.70f, 0.30f, 0.85f, 0};
    tracklet_arr[1].state = HAILO_TRACKLET_STATE_TRACKED;
    tracklet_arr[1].frames_since_update = 0;
    tracklet_arr[1].total_frames_tracked = 5;
    tracklet_arr[1].velocity_x = 0.0f;
    tracklet_arr[1].velocity_y = 0.0f;

    tracklet_arr[2].id = 3;
    tracklet_arr[2].detection = {0.30f, 0.40f, 0.50f, 0.60f, 0.7f, 0};
    tracklet_arr[2].state = HAILO_TRACKLET_STATE_LOST;
    tracklet_arr[2].frames_since_update = 3;
    tracklet_arr[2].total_frames_tracked = 10;
    tracklet_arr[2].velocity_x = 0.0f;
    tracklet_arr[2].velocity_y = 0.0f;

    hailo_tracklets_t tracklets;
    tracklets.tracklets = tracklet_arr;
    tracklets.count = 3;

    auto result = stage.tracklets_to_hailo_detections(tracklets);
    ASSERT_EQ(result.size(), 1u);

    auto ids = hailo_common::get_hailo_track_id(result[0]);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0]->get_id(), 2);
}

TEST_F(DetectionTrackerStageTest, TrackletsToHailoDetectionsUnknownLabel)
{
    std::map<uint8_t, std::string> labels = {{0, "person"}};
    hailo_tracker_config_t config = HAILO_TRACKER_CONFIG_DEFAULT;
    TestableDetectionTrackerStage stage("unknown_label_test", config, labels);

    // Normalized [0, 1] — see TrackletsToHailoDetections.
    hailo_tracklet_t tracklet_arr[1];
    tracklet_arr[0].id = 7;
    tracklet_arr[0].detection = {0.10f, 0.20f, 0.30f, 0.40f, 0.9f, 5}; // class_id 5 not in labels
    tracklet_arr[0].state = HAILO_TRACKLET_STATE_TRACKED;
    tracklet_arr[0].frames_since_update = 0;
    tracklet_arr[0].total_frames_tracked = 1;
    tracklet_arr[0].velocity_x = 0.0f;
    tracklet_arr[0].velocity_y = 0.0f;

    hailo_tracklets_t tracklets;
    tracklets.tracklets = tracklet_arr;
    tracklets.count = 1;

    auto result = stage.tracklets_to_hailo_detections(tracklets);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->get_label(), "");
    EXPECT_EQ(result[0]->get_class_id(), 5);
}
