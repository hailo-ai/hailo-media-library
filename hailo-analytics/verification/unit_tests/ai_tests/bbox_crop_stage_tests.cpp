#include <gtest/gtest.h>
#include <hailodsp.h>
#include <stddef.h>
#include <stdint.h>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include <utility>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/dsp_utils.hpp"
#include "test_tier.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/cropping/dsp_cropping.hpp"

namespace
{

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
// BBoxCropStage DSP scale check
//
// Verifies that BBoxCropStage handles narrow bboxes that would violate the
// DSP's dst_dim / src_dim <= 32 rule on either axis.
// =============================================================================

namespace
{

struct NarrowBboxCase
{
    int out_w;
    int out_h;
    float xmin;
    float ymin;
    float w;
    float h;
    pipeline::AppStatus expected_status;
    const char *id;
};

class BBoxCropDspScaleTest : public ::testing::TestWithParam<NarrowBboxCase>
{
  protected:
    pipeline::BufferPtr make_real_dsp_input(uint32_t width, uint32_t height)
    {
        const auto stride = dsp_utils::get_dsp_desired_stride_from_width(width);
        m_input_pool = std::make_shared<MediaLibraryBufferPool>(width, height, HAILO_FORMAT_NV12,
                                                                /*max_buffers=*/2, HAILO_MEMORY_TYPE_DMABUF, stride,
                                                                "test_dsp_scale_input_pool");
        if (m_input_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            return nullptr;
        }
        auto media_buf = std::make_shared<hailo_media_library_buffer>();
        if (m_input_pool->acquire_buffer(media_buf) != MEDIA_LIBRARY_SUCCESS)
        {
            return nullptr;
        }
        auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
        return std::make_shared<pipeline::Buffer>(media_buf, roi);
    }

    MediaLibraryBufferPoolPtr m_input_pool;
};

TEST_P(BBoxCropDspScaleTest, PR_NIGHTLY_TIER(NarrowBboxIsHandled))
{
    const auto p = GetParam();

    auto stage = std::make_shared<pipeline::cropping::BBoxCropStage>(
        "test_dsp_scale", /*output_pool_size=*/4, /*input_width=*/FRAME_W, /*input_height=*/FRAME_H,
        /*output_width=*/p.out_w, /*output_height=*/p.out_h, /*main_sub_name=*/"main", /*sub_sub_name=*/"sub",
        std::vector<std::string>{"person"}, /*queue_size=*/1, /*leaky=*/false, /*trace_processing_operations=*/false,
        pipeline::StagePoolMode::FAIL_ON_EMPTY_POOL, /*crop_every_x_frames=*/1, /*use_letterbox=*/false,
        DSP_LETTERBOX_MIDDLE, pipeline::cropping::DEFAULT_LETTERBOX_COLOR, /*max_crops=*/4);

    ASSERT_EQ(stage->init(), pipeline::AppStatus::SUCCESS) << p.id;

    auto buffer = make_real_dsp_input(FRAME_W, FRAME_H);
    ASSERT_NE(buffer, nullptr) << "failed to allocate real DSP input buffer for " << p.id;
    buffer->get_roi()->add_object(
        std::make_shared<HailoDetection>(HailoBBox(p.xmin, p.ymin, p.w, p.h), /*class_id=*/1, "person", 0.9f));

    EXPECT_EQ(stage->process(buffer), p.expected_status) << p.id;
}

INSTANTIATE_TEST_SUITE_P(
    NarrowBboxes, BBoxCropDspScaleTest,
    ::testing::ValuesIn(std::vector<NarrowBboxCase>{
        // ---- width-narrow rows (object leaving left/right edge, or directly narrow) ----
        {192, 192, 0.500f, 0.500f, 0.00208f, 0.050f, pipeline::AppStatus::SUCCESS, "FL_w_directly_narrow_4px"},
        // xmin+w == 0.022 -> on-frame portion = 0.002*1920 = 3.84 px (even-aligned to 4)
        {192, 192, -0.020f, 0.500f, 0.022f, 0.050f, pipeline::AppStatus::SUCCESS, "FL_w_clipped_left_edge"},
        {192, 192, 0.998f, 0.500f, 0.050f, 0.050f, pipeline::AppStatus::SUCCESS, "FL_w_clipped_right_edge"},
        {192, 192, 0.500f, 0.500f, 0.00313f, 0.050f, pipeline::AppStatus::SUCCESS, "FL_w_at_threshold_6px"},
        {256, 256, 0.500f, 0.500f, 0.00313f, 0.050f, pipeline::AppStatus::SUCCESS, "DPM256_w_narrow_6px"},
        {320, 320, 0.500f, 0.500f, 0.00417f, 0.050f, pipeline::AppStatus::SUCCESS, "DPM320_w_narrow_8px"},
        // ---- height-narrow rows (object leaving top/bottom edge, or directly short) ----
        {192, 192, 0.500f, 0.500f, 0.050f, 0.00370f, pipeline::AppStatus::SUCCESS, "FL_h_directly_narrow_4px"},
        // ymin+h == 0.022 -> on-frame portion = 0.002*1080 = 2.16 px (clamped to 2)
        {192, 192, 0.500f, -0.020f, 0.050f, 0.022f, pipeline::AppStatus::SUCCESS, "FL_h_clipped_top_edge"},
        {192, 192, 0.500f, 0.998f, 0.050f, 0.050f, pipeline::AppStatus::SUCCESS, "FL_h_clipped_bottom_edge"},
        {192, 192, 0.500f, 0.500f, 0.050f, 0.00556f, pipeline::AppStatus::SUCCESS, "FL_h_at_threshold_6px"},
        {256, 256, 0.500f, 0.500f, 0.050f, 0.00556f, pipeline::AppStatus::SUCCESS, "DPM256_h_narrow_6px"},
        {320, 320, 0.500f, 0.500f, 0.050f, 0.00741f, pipeline::AppStatus::SUCCESS, "DPM320_h_narrow_8px"},
        // ---- both-axes and safe sanity rows ----
        {192, 192, 0.500f, 0.500f, 0.00208f, 0.00370f, pipeline::AppStatus::SUCCESS, "FL_wh_both_narrow"},
        {192, 192, 0.400f, 0.400f, 0.10f, 0.10f, pipeline::AppStatus::SUCCESS, "FL_safely_wide"},
        {256, 256, 0.300f, 0.300f, 0.05f, 0.05f, pipeline::AppStatus::SUCCESS, "DPM256_safely_wide"},
    }),
    [](const ::testing::TestParamInfo<NarrowBboxCase> &info) { return std::string(info.param.id); });

} // namespace
