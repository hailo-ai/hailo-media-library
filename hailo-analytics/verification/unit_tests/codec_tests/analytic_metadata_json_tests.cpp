#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

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

BufferPtr make_empty_buffer()
{
    return std::make_shared<Buffer>(make_mock_buffer());
}

} // namespace

TEST(AnalyticMetadataJsonTest, EmptyROIReturnsEmptyJson)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    auto buffer = make_buffer_with_roi(roi);

    auto result = build_metadata_json(buffer);
    EXPECT_TRUE(result.empty());
}

TEST(AnalyticMetadataJsonTest, SingleDetection)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    auto det = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), "person", 0.95f);
    roi->add_object(det);

    auto result = build_metadata_json(make_buffer_with_roi(roi));

    ASSERT_FALSE(result.empty());
    ASSERT_TRUE(result.contains(analytic_metadata_fields::DETECTIONS));
    auto &detections = result[analytic_metadata_fields::DETECTIONS];
    ASSERT_EQ(detections.size(), 1);

    auto &d = detections[0];
    EXPECT_EQ(d[analytic_metadata_fields::detection::LABEL].get<std::string>(), "person");
    EXPECT_FLOAT_EQ(d[analytic_metadata_fields::detection::DETECTION_CONFIDENCE].get<float>(), 0.95f);

    auto &bbox = d[analytic_metadata_fields::detection::BBOX];
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::XMIN].get<float>(), 0.1f * FRAME_W);
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::YMIN].get<float>(), 0.2f * FRAME_H);
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::XMAX].get<float>(), (0.1f + 0.3f) * FRAME_W);
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::YMAX].get<float>(), (0.2f + 0.4f) * FRAME_H);
}

TEST(AnalyticMetadataJsonTest, MultipleDetections)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), "person", 0.9f));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.5f, 0.5f, 0.2f, 0.2f), "face", 0.85f));

    auto result = build_metadata_json(make_buffer_with_roi(roi));
    ASSERT_EQ(result[analytic_metadata_fields::DETECTIONS].size(), 2);
    EXPECT_EQ(result[analytic_metadata_fields::DETECTIONS][0][analytic_metadata_fields::detection::LABEL], "person");
    EXPECT_EQ(result[analytic_metadata_fields::DETECTIONS][1][analytic_metadata_fields::detection::LABEL], "face");
}

TEST(AnalyticMetadataJsonTest, SingleLandmark)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    std::vector<HailoPoint> points = {HailoPoint(0.5f, 0.5f, 0.99f), HailoPoint(0.6f, 0.7f, 0.98f)};
    std::vector<std::pair<int, int>> pairs = {{0, 1}};
    auto lm = std::make_shared<HailoLandmarks>("face_landmarks", points, 0.5f, pairs);
    roi->add_object(lm);

    auto result = build_metadata_json(make_buffer_with_roi(roi));

    ASSERT_TRUE(result.contains(analytic_metadata_fields::LANDMARKS));
    auto &landmarks = result[analytic_metadata_fields::LANDMARKS];
    ASSERT_EQ(landmarks.size(), 1);

    auto &l = landmarks[0];
    EXPECT_EQ(l[analytic_metadata_fields::landmark::POINTS_FORMAT],
              analytic_metadata_fields::landmark::POINTS_FORMAT_VALUE);
    EXPECT_EQ(l[analytic_metadata_fields::landmark::POINTS_STRIDE],
              analytic_metadata_fields::landmark::POINTS_STRIDE_VALUE);

    auto &pts = l[analytic_metadata_fields::landmark::POINTS];
    ASSERT_EQ(pts.size(), 6); // 2 points * 3 (x, y, conf)
    EXPECT_FLOAT_EQ(pts[0].get<float>(), 0.5f * FRAME_W);
    EXPECT_FLOAT_EQ(pts[1].get<float>(), 0.5f * FRAME_H);
    EXPECT_FLOAT_EQ(pts[2].get<float>(), 0.99f);

    auto &pr = l[analytic_metadata_fields::landmark::PAIRS];
    ASSERT_EQ(pr.size(), 2); // 1 pair * 2
    EXPECT_EQ(pr[0].get<int>(), 0);
    EXPECT_EQ(pr[1].get<int>(), 1);
}

TEST(AnalyticMetadataJsonTest, LandmarksWithEmptyPointsSkipped)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    std::vector<HailoPoint> empty_points;
    std::vector<std::pair<int, int>> empty_pairs;
    auto lm = std::make_shared<HailoLandmarks>("empty", empty_points, 0.5f, empty_pairs);
    roi->add_object(lm);

    auto result = build_metadata_json(make_buffer_with_roi(roi));
    EXPECT_TRUE(result.empty());
}

TEST(AnalyticMetadataJsonTest, DetectionWithNestedLandmarks)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));

    auto det = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.5f), "face", 0.9f);
    std::vector<HailoPoint> points = {HailoPoint(0.5f, 0.5f, 1.0f)};
    std::vector<std::pair<int, int>> pairs;
    auto lm = std::make_shared<HailoLandmarks>("face_lm", points, 0.5f, pairs);
    det->add_object(lm);
    roi->add_object(det);

    auto result = build_metadata_json(make_buffer_with_roi(roi));

    ASSERT_TRUE(result.contains(analytic_metadata_fields::DETECTIONS));
    auto &det_json = result[analytic_metadata_fields::DETECTIONS][0];

    ASSERT_TRUE(det_json.contains(analytic_metadata_fields::LANDMARKS));
    auto &nested_lm = det_json[analytic_metadata_fields::LANDMARKS];
    ASSERT_EQ(nested_lm.size(), 1);

    // Point coords should be transformed using flattened detection bbox (0,0,0.5,0.5)
    // scale_x = 0.5 * 1920 = 960, offset_x = 0 * 1920 = 0
    // point x = 0.5 * 960 + 0 = 480
    auto &pts = nested_lm[0][analytic_metadata_fields::landmark::POINTS];
    EXPECT_FLOAT_EQ(pts[0].get<float>(), 0.5f * 0.5f * FRAME_W);
    EXPECT_FLOAT_EQ(pts[1].get<float>(), 0.5f * 0.5f * FRAME_H);
}

TEST(AnalyticMetadataJsonTest, CoordinateTransformWithSubROI)
{
    // ROI covering the right half of the frame
    auto roi = std::make_shared<HailoROI>(HailoBBox(0.5f, 0.0f, 0.5f, 1.0f));
    auto det = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f), "full", 1.0f);
    roi->add_object(det);

    auto result = build_metadata_json(make_buffer_with_roi(roi));
    auto &bbox = result[analytic_metadata_fields::DETECTIONS][0][analytic_metadata_fields::detection::BBOX];

    // xmin = ((0.0 * 0.5) + 0.5) * 1920 = 960
    // ymin = ((0.0 * 1.0) + 0.0) * 1080 = 0
    // xmax = ((1.0 * 0.5) + 0.5) * 1920 = 1920
    // ymax = ((1.0 * 1.0) + 0.0) * 1080 = 1080
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::XMIN].get<float>(), 960.0f);
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::YMIN].get<float>(), 0.0f);
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::XMAX].get<float>(), 1920.0f);
    EXPECT_FLOAT_EQ(bbox[analytic_metadata_fields::detection::bbox::YMAX].get<float>(), 1080.0f);
}

TEST(AnalyticMetadataJsonTest, MetadataFieldsPresent)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0, 0, 0.1f, 0.1f), "obj", 0.5f));

    auto result = build_metadata_json(make_buffer_with_roi(roi));

    EXPECT_EQ(result[analytic_metadata_fields::ISP_TIMESTAMP].get<uint64_t>(), TEST_TIMESTAMP);
    EXPECT_EQ(result[analytic_metadata_fields::FRAME_WIDTH].get<uint32_t>(), FRAME_W);
    EXPECT_EQ(result[analytic_metadata_fields::FRAME_HEIGHT].get<uint32_t>(), FRAME_H);
}

TEST(AnalyticMetadataJsonTest, NestedDetections)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    auto person = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.5f), "person", 0.9f);
    auto face = std::make_shared<HailoDetection>(HailoBBox(0.2f, 0.2f, 0.3f, 0.3f), "face", 0.8f);
    person->add_object(face);
    roi->add_object(person);

    auto result = build_metadata_json(make_buffer_with_roi(roi));

    auto &person_json = result[analytic_metadata_fields::DETECTIONS][0];
    EXPECT_EQ(person_json[analytic_metadata_fields::detection::LABEL], "person");
    ASSERT_TRUE(person_json.contains(analytic_metadata_fields::DETECTIONS));

    auto &face_json = person_json[analytic_metadata_fields::DETECTIONS][0];
    EXPECT_EQ(face_json[analytic_metadata_fields::detection::LABEL], "face");
}
