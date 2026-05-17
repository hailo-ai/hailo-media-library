#include <gtest/gtest.h>
#include <chrono>

#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "analytics_metadata.pb.h"

using namespace hailo_analytics::pipeline;
using namespace hailo_analytics::pipeline::codecs;

namespace
{

constexpr uint32_t FRAME_W = 1920;
constexpr uint32_t FRAME_H = 1080;
constexpr uint64_t TEST_TIMESTAMP = 123456789;

HailoMediaLibraryBufferPtr make_mock_buffer(uint32_t width = FRAME_W, uint32_t height = FRAME_H)
{
    auto buf = std::make_shared<hailo_media_library_buffer>();
    buf->buffer_data = std::make_shared<hailo_buffer_data_t>(width, height, 0, HailoFormat{}, HailoMemoryType{},
                                                             std::vector<hailo_data_plane_t>{});
    buf->isp_timestamp_ns = TEST_TIMESTAMP;
    return buf;
}

BufferPtr make_buffer_with_roi(HailoROIPtr roi, uint32_t width = FRAME_W, uint32_t height = FRAME_H)
{
    return std::make_shared<Buffer>(make_mock_buffer(width, height), roi);
}

// Build a proto and round-trip through SerializeToString / ParseFromString to validate
// the on-the-wire bytes (not just the in-memory message).
hailo_analytics::Frame build_and_roundtrip(BufferPtr buffer, bool *out_populated = nullptr)
{
    hailo_analytics::Frame source;
    bool populated = build_metadata_proto(buffer, source);
    if (out_populated != nullptr)
        *out_populated = populated;

    std::string serialized;
    EXPECT_TRUE(source.SerializeToString(&serialized));

    hailo_analytics::Frame parsed;
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
}

} // namespace

TEST(AnalyticMetadataProtoTest, EmptyROIReturnsFalse)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    auto buffer = make_buffer_with_roi(roi);

    hailo_analytics::Frame frame;
    EXPECT_FALSE(build_metadata_proto(buffer, frame));
    EXPECT_EQ(frame.detections_size(), 0);
    EXPECT_EQ(frame.landmarks_size(), 0);
    EXPECT_EQ(frame.classifications_size(), 0);
}

TEST(AnalyticMetadataProtoTest, SingleDetection)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), "person", 0.95f));

    bool populated = false;
    auto frame = build_and_roundtrip(make_buffer_with_roi(roi), &populated);
    EXPECT_TRUE(populated);

    ASSERT_EQ(frame.detections_size(), 1);
    const auto &d = frame.detections(0);
    EXPECT_EQ(d.label(), "person");
    EXPECT_FLOAT_EQ(d.confidence(), 0.95f);

    EXPECT_FLOAT_EQ(d.bbox().xmin(), 0.1f * FRAME_W);
    EXPECT_FLOAT_EQ(d.bbox().ymin(), 0.2f * FRAME_H);
    EXPECT_FLOAT_EQ(d.bbox().xmax(), (0.1f + 0.3f) * FRAME_W);
    EXPECT_FLOAT_EQ(d.bbox().ymax(), (0.2f + 0.4f) * FRAME_H);
}

TEST(AnalyticMetadataProtoTest, MultipleDetections)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), "person", 0.9f));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.5f, 0.5f, 0.2f, 0.2f), "face", 0.85f));

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));
    ASSERT_EQ(frame.detections_size(), 2);
    EXPECT_EQ(frame.detections(0).label(), "person");
    EXPECT_EQ(frame.detections(1).label(), "face");
}

TEST(AnalyticMetadataProtoTest, SingleLandmark)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    std::vector<HailoPoint> points = {HailoPoint(0.5f, 0.5f, 0.99f), HailoPoint(0.6f, 0.7f, 0.98f)};
    std::vector<std::pair<int, int>> pairs = {{0, 1}};
    roi->add_object(std::make_shared<HailoLandmarks>("face_landmarks", points, 0.5f, pairs));

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));

    ASSERT_EQ(frame.landmarks_size(), 1);
    const auto &lm = frame.landmarks(0);
    EXPECT_EQ(lm.points_format(), "x,y,conf");
    EXPECT_EQ(lm.points_stride(), 3u);

    ASSERT_EQ(lm.points_size(), 6); // 2 points * (x, y, conf)
    EXPECT_FLOAT_EQ(lm.points(0), 0.5f * FRAME_W);
    EXPECT_FLOAT_EQ(lm.points(1), 0.5f * FRAME_H);
    EXPECT_FLOAT_EQ(lm.points(2), 0.99f);

    ASSERT_EQ(lm.pairs_size(), 2); // 1 pair * 2
    EXPECT_EQ(lm.pairs(0), 0u);
    EXPECT_EQ(lm.pairs(1), 1u);
}

TEST(AnalyticMetadataProtoTest, LandmarksWithEmptyPointsSkipped)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    std::vector<HailoPoint> empty_points;
    std::vector<std::pair<int, int>> empty_pairs;
    roi->add_object(std::make_shared<HailoLandmarks>("empty", empty_points, 0.5f, empty_pairs));

    hailo_analytics::Frame frame;
    EXPECT_FALSE(build_metadata_proto(make_buffer_with_roi(roi), frame));
}

TEST(AnalyticMetadataProtoTest, DetectionWithNestedLandmarks)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));

    auto det = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.5f), "face", 0.9f);
    std::vector<HailoPoint> points = {HailoPoint(0.5f, 0.5f, 1.0f)};
    std::vector<std::pair<int, int>> pairs;
    det->add_object(std::make_shared<HailoLandmarks>("face_lm", points, 0.5f, pairs));
    roi->add_object(det);

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));

    ASSERT_EQ(frame.detections_size(), 1);
    const auto &face = frame.detections(0);
    ASSERT_EQ(face.landmarks_size(), 1);

    // Point coords should be transformed using flattened detection bbox (0,0,0.5,0.5):
    // scale_x = 0.5 * 1920 = 960, offset_x = 0; point x = 0.5 * 960 + 0 = 480.
    const auto &lm = face.landmarks(0);
    ASSERT_EQ(lm.points_size(), 3);
    EXPECT_FLOAT_EQ(lm.points(0), 0.5f * 0.5f * FRAME_W);
    EXPECT_FLOAT_EQ(lm.points(1), 0.5f * 0.5f * FRAME_H);
}

TEST(AnalyticMetadataProtoTest, CoordinateTransformWithSubROI)
{
    // ROI covering the right half of the frame.
    auto roi = std::make_shared<HailoROI>(HailoBBox(0.5f, 0.0f, 0.5f, 1.0f));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f), "full", 1.0f));

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));
    ASSERT_EQ(frame.detections_size(), 1);
    const auto &b = frame.detections(0).bbox();

    EXPECT_FLOAT_EQ(b.xmin(), 960.0f);
    EXPECT_FLOAT_EQ(b.ymin(), 0.0f);
    EXPECT_FLOAT_EQ(b.xmax(), 1920.0f);
    EXPECT_FLOAT_EQ(b.ymax(), 1080.0f);
}

TEST(AnalyticMetadataProtoTest, FrameMetadataFieldsPresent)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0, 0, 0.1f, 0.1f), "obj", 0.5f));

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));
    EXPECT_EQ(frame.isp_timestamp_ns(), TEST_TIMESTAMP);
    EXPECT_EQ(frame.frame_width(), FRAME_W);
    EXPECT_EQ(frame.frame_height(), FRAME_H);
}

TEST(AnalyticMetadataProtoTest, NestedDetections)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    auto person = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.5f), "person", 0.9f);
    auto face = std::make_shared<HailoDetection>(HailoBBox(0.2f, 0.2f, 0.3f, 0.3f), "face", 0.8f);
    person->add_object(face);
    roi->add_object(person);

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));
    ASSERT_EQ(frame.detections_size(), 1);
    EXPECT_EQ(frame.detections(0).label(), "person");
    ASSERT_EQ(frame.detections(0).detections_size(), 1);
    EXPECT_EQ(frame.detections(0).detections(0).label(), "face");
}

TEST(AnalyticMetadataProtoTest, TrackingIdAttachedToDetection)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    auto det = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.5f), "person", 0.9f);
    det->add_object(std::make_shared<HailoUniqueID>(42));
    roi->add_object(det);

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));
    ASSERT_EQ(frame.detections_size(), 1);
    ASSERT_TRUE(frame.detections(0).has_tracking_id());
    EXPECT_EQ(frame.detections(0).tracking_id(), 42u);
}

TEST(AnalyticMetadataProtoTest, ClassificationAttachedToFrameAndDetection)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoClassification>("scene", "indoor", 0.7f));

    auto det = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.5f), "person", 0.9f);
    det->add_object(std::make_shared<HailoClassification>("gender", "male", 0.6f));
    roi->add_object(det);

    auto frame = build_and_roundtrip(make_buffer_with_roi(roi));
    ASSERT_EQ(frame.classifications_size(), 1);
    EXPECT_EQ(frame.classifications(0).type(), "scene");
    EXPECT_EQ(frame.classifications(0).label(), "indoor");

    ASSERT_EQ(frame.detections_size(), 1);
    ASSERT_EQ(frame.detections(0).classifications_size(), 1);
    EXPECT_EQ(frame.detections(0).classifications(0).type(), "gender");
    EXPECT_EQ(frame.detections(0).classifications(0).label(), "male");
}

// Lightweight perf check: build + serialize a representative payload many times and
// assert it stays well under the legacy ~20 ms/frame budget. The threshold is loose
// (5 ms average) so it tolerates noisy CI; a real regression would blow past it.
TEST(AnalyticMetadataProtoTest, SerializeManyFacesUnderBudget)
{
    static constexpr int NUM_FACES = 50;
    static constexpr int LANDMARKS_PER_FACE = 98;
    static constexpr int ITERATIONS = 200;
    static constexpr double MAX_AVG_MS = 5.0;

    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    for (int face_index = 0; face_index < NUM_FACES; face_index++)
    {
        float x_offset = static_cast<float>(face_index % 10) * 0.1f;
        float y_offset = static_cast<float>(face_index / 10) * 0.2f;
        auto person = std::make_shared<HailoDetection>(HailoBBox(x_offset, y_offset, 0.08f, 0.15f), "person", 0.9f);
        auto face = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.0f, 0.6f, 0.4f), "face", 0.85f);

        std::vector<HailoPoint> points;
        points.reserve(LANDMARKS_PER_FACE);
        for (int point_index = 0; point_index < LANDMARKS_PER_FACE; point_index++)
        {
            float frac = static_cast<float>(point_index) / static_cast<float>(LANDMARKS_PER_FACE);
            points.emplace_back(frac, 1.0f - frac, 0.95f);
        }
        std::vector<std::pair<int, int>> pairs;
        for (int pair_index = 0; pair_index < LANDMARKS_PER_FACE - 1; pair_index++)
            pairs.emplace_back(pair_index, pair_index + 1);

        face->add_object(std::make_shared<HailoLandmarks>("face_landmarks_98", points, 0.5f, pairs));
        person->add_object(face);
        roi->add_object(person);
    }
    auto buffer = make_buffer_with_roi(roi);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < ITERATIONS; i++)
    {
        hailo_analytics::Frame frame;
        ASSERT_TRUE(build_metadata_proto(buffer, frame));

        std::string serialized;
        ASSERT_TRUE(frame.SerializeToString(&serialized));
        ASSERT_FALSE(serialized.empty());
    }
    auto elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / ITERATIONS;

    EXPECT_LT(elapsed_ms, MAX_AVG_MS) << "Average serialize time " << elapsed_ms << " ms exceeds budget " << MAX_AVG_MS
                                      << " ms";
}
