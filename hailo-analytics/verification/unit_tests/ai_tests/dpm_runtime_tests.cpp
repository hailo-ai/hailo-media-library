#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "test_tier.hpp"

namespace
{

namespace dpm = hailo_analytics::analytics::dpm_analytics;
namespace pipeline = hailo_analytics::pipeline;

constexpr uint32_t FRAME_W = 1920;
constexpr uint32_t FRAME_H = 1080;
constexpr int OUTPUT_W = 128;
constexpr int OUTPUT_H = 128;

HailoMediaLibraryBufferPtr make_mock_buffer(uint32_t width = FRAME_W, uint32_t height = FRAME_H)
{
    auto buf = std::make_shared<hailo_media_library_buffer>();
    buf->buffer_data = std::make_shared<hailo_buffer_data_t>(width, height, 0, HailoFormat{}, HailoMemoryType{},
                                                             std::vector<hailo_data_plane_t>{});
    return buf;
}

pipeline::BufferPtr make_buffer_with_detections(const std::vector<std::tuple<std::string, float>> &detections)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    int class_id = 1;
    for (const auto &[label, conf] : detections)
    {
        HailoBBox bbox(0.1f, 0.1f, 0.2f, 0.2f);
        roi->add_object(std::make_shared<HailoDetection>(bbox, class_id++, label, conf));
    }
    return std::make_shared<pipeline::Buffer>(make_mock_buffer(), roi);
}

std::shared_ptr<pipeline::cropping::BBoxCropStage> build_bbox_crop_stage(std::vector<std::string> labels,
                                                                         size_t max_crops)
{
    return std::make_shared<pipeline::cropping::BBoxCropStage>(
        "test_bbox_crop", /*output_pool_size=*/1, /*input_width=*/FRAME_W, /*input_height=*/FRAME_H,
        /*output_width=*/OUTPUT_W, /*output_height=*/OUTPUT_H, /*main_sub_name=*/"main", /*sub_sub_name=*/"sub",
        std::move(labels), /*queue_size=*/1, /*leaky=*/false, /*trace_processing_operations=*/false,
        pipeline::StagePoolMode::FAIL_ON_EMPTY_POOL, /*crop_every_x_frames=*/1, /*use_letterbox=*/false,
        DSP_LETTERBOX_MIDDLE, pipeline::cropping::DEFAULT_LETTERBOX_COLOR, max_crops);
}

} // namespace

// =============================================================================
// BBoxCropStage::set_max_crops
// =============================================================================

TEST(BBoxCropStageMaxCropsTest, PR_NIGHTLY_TIER(CapHonouredAndOverCapStaysOnRoi))
{
    auto stage = build_bbox_crop_stage({"person"}, /*max_crops=*/3);
    auto buffer = make_buffer_with_detections({
        {"person", 0.90f},
        {"person", 0.85f},
        {"person", 0.80f},
        {"person", 0.75f},
        {"person", 0.70f},
        {"vehicle", 0.95f},
        {"vehicle", 0.50f},
    });

    std::vector<dsp_crop_api_t> dims;
    stage->prepare_crops(buffer, dims);

    EXPECT_EQ(dims.size(), 3u);
    EXPECT_EQ(buffer->get_roi()->get_objects().size(), 7u);
    stage->post_crop(buffer);
}

TEST(BBoxCropStageMaxCropsTest, PR_NIGHTLY_TIER(SetMaxCropsTakesEffectOnNextPrepare))
{
    auto stage = build_bbox_crop_stage({"person"}, /*max_crops=*/3);
    auto buffer = make_buffer_with_detections({
        {"person", 0.90f},
        {"person", 0.85f},
        {"person", 0.80f},
        {"person", 0.75f},
        {"person", 0.70f},
    });

    std::vector<dsp_crop_api_t> dims;
    stage->prepare_crops(buffer, dims);
    EXPECT_EQ(dims.size(), 3u);
    EXPECT_EQ(stage->get_max_crops(), 3u);
    stage->post_crop(buffer);

    stage->set_max_crops(5);
    EXPECT_EQ(stage->get_max_crops(), 5u);

    dims.clear();
    stage->prepare_crops(buffer, dims);
    EXPECT_EQ(dims.size(), 5u);
    stage->post_crop(buffer);
}

TEST(BBoxCropStageMaxCropsTest, PR_NIGHTLY_TIER(MaxCropsUncappedKeepsAllMatching))
{
    auto stage = build_bbox_crop_stage({"person"}, std::numeric_limits<size_t>::max());
    auto buffer = make_buffer_with_detections({
        {"person", 0.9f},
        {"vehicle", 0.95f},
        {"face", 0.9f},
        {"vehicle", 0.5f},
    });

    std::vector<dsp_crop_api_t> dims;
    stage->prepare_crops(buffer, dims);

    EXPECT_EQ(dims.size(), 1u);
    EXPECT_EQ(buffer->get_roi()->get_objects().size(), 4u);
    stage->post_crop(buffer);
}

// =============================================================================
// DetectorLabelFilter::set_labels
// =============================================================================

TEST(DetectorLabelFilterTest, PR_NIGHTLY_TIER(InitialLabelsApplied))
{
    auto stage = std::make_shared<dpm::DetectorLabelFilter>("test_filter", /*queue_size=*/1, /*leaky=*/false,
                                                            std::vector<std::string>{"person"});
    auto buffer = make_buffer_with_detections({{"person", 0.9f}, {"vehicle", 0.8f}, {"face", 0.7f}});

    stage->process(buffer);

    auto detections = hailo_common::get_hailo_detections(buffer->get_roi());
    ASSERT_EQ(detections.size(), 1u);
    EXPECT_EQ(detections[0]->get_label(), "person");
}

TEST(DetectorLabelFilterTest, PR_NIGHTLY_TIER(SetLabelsObservedOnNextProcess))
{
    auto stage = std::make_shared<dpm::DetectorLabelFilter>("test_filter", /*queue_size=*/1, /*leaky=*/false,
                                                            std::vector<std::string>{"person"});

    {
        auto buffer = make_buffer_with_detections({{"person", 0.9f}, {"vehicle", 0.8f}});
        stage->process(buffer);
        auto detections = hailo_common::get_hailo_detections(buffer->get_roi());
        ASSERT_EQ(detections.size(), 1u);
        EXPECT_EQ(detections[0]->get_label(), "person");
    }

    stage->set_labels({"vehicle"});
    auto current = stage->get_labels();
    ASSERT_NE(current, nullptr);
    ASSERT_EQ(current->size(), 1u);
    EXPECT_EQ((*current)[0], "vehicle");

    {
        auto buffer = make_buffer_with_detections({{"person", 0.9f}, {"vehicle", 0.8f}});
        stage->process(buffer);
        auto detections = hailo_common::get_hailo_detections(buffer->get_roi());
        ASSERT_EQ(detections.size(), 1u);
        EXPECT_EQ(detections[0]->get_label(), "vehicle");
    }
}

TEST(DetectorLabelFilterTest, PR_NIGHTLY_TIER(EmptyLabelsLetEverythingThrough))
{
    auto stage = std::make_shared<dpm::DetectorLabelFilter>("test_filter", /*queue_size=*/1, /*leaky=*/false,
                                                            std::vector<std::string>{});
    auto buffer = make_buffer_with_detections({{"person", 0.9f}, {"vehicle", 0.8f}, {"face", 0.7f}});

    stage->process(buffer);

    auto detections = hailo_common::get_hailo_detections(buffer->get_roi());
    EXPECT_EQ(detections.size(), 3u);
}
