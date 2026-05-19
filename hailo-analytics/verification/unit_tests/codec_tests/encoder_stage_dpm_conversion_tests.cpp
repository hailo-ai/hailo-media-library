#include <gtest/gtest.h>
#include <hailort.h>
#include <stddef.h>
#include <stdint.h>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "media_library/analytics_metadata.hpp"
#include "media_library/buffer_pool.hpp"
#include "test_tier.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

using hailo_analytics::pipeline::Buffer;
using hailo_analytics::pipeline::BufferPtr;
using hailo_analytics::pipeline::codecs::EncoderStage;
using hailo_analytics::pipeline::codecs::EncoderStageBuild;

namespace
{

struct MaskHolder
{
    std::vector<uint8_t> data;
};

HailoROIPtr build_roi_with_masked_detection(const std::string &label, int mask_w, int mask_h,
                                            std::shared_ptr<MaskHolder> &mask_holder_out)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f));
    auto det = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), label, 0.9f);
    mask_holder_out = std::make_shared<MaskHolder>();
    mask_holder_out->data.assign(static_cast<size_t>(mask_w) * static_cast<size_t>(mask_h), 1);
    auto mask = std::make_shared<HailoClassMask>(mask_holder_out->data.data(), mask_w, mask_h, 0.5f);
    det->add_object(mask);
    roi->add_object(det);
    return roi;
}

HailoROIPtr build_roi_with_unmasked_detection(const std::string &label, const HailoBBox &bbox)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f));
    auto det = std::make_shared<HailoDetection>(bbox, label, 0.9f);
    roi->add_object(det);
    return roi;
}

// Build an ROI with a single detection that owns `num_class_masks` HailoClassMask children, one
// per segmentor output class. Each mask gets a distinct byte payload (i+1) so a test can verify
// which child was picked. The MaskHolders are returned to keep the source byte buffers alive
// for the duration of the test (HailoClassMask only stores a raw pointer to them).
HailoROIPtr build_roi_with_multi_mask_detection(const std::string &label, int num_class_masks, int mask_w, int mask_h,
                                                std::vector<std::shared_ptr<MaskHolder>> &holders_out)
{
    auto roi = std::make_shared<HailoROI>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f));
    auto det = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), label, 0.9f);
    holders_out.clear();
    for (int i = 0; i < num_class_masks; ++i)
    {
        auto holder = std::make_shared<MaskHolder>();
        holder->data.assign(static_cast<size_t>(mask_w) * static_cast<size_t>(mask_h), static_cast<uint8_t>(i + 1));
        det->add_object(std::make_shared<HailoClassMask>(holder->data.data(), mask_w, mask_h, 0.5f));
        holders_out.push_back(holder);
    }
    roi->add_object(det);
    return roi;
}

HailoROIPtr build_empty_roi()
{
    return std::make_shared<HailoROI>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f));
}

// EncoderStage now reads pixel dimensions for the wire-types from the buffer itself
// (buffer_data->width/height). Tests stub a minimal buffer_data so the conversion has dims.
BufferPtr make_buffer(HailoROIPtr roi, size_t frame_w, size_t frame_h)
{
    auto mlib = std::make_shared<hailo_media_library_buffer>();
    mlib->buffer_data = std::make_shared<hailo_buffer_data_t>(frame_w, frame_h, /*planes_count*/ 0, HailoFormat{},
                                                              HailoMemoryType{}, std::vector<hailo_data_plane_t>{});
    return std::make_shared<Buffer>(mlib, std::move(roi));
}

std::shared_ptr<EncoderStage> make_conversion_stage()
{
    return EncoderStageBuild::create()
        .set_stage_name("test_dpm_encoder")
        .set_attach_analytics_metadata(true)
        .buildptr();
}

} // namespace

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(WritesSemanticSegmentationFromMaskedDetection))
{
    auto stage = make_conversion_stage();

    std::shared_ptr<MaskHolder> mask_holder;
    auto roi = build_roi_with_masked_detection("person", /*mask_w*/ 16, /*mask_h*/ 16, mask_holder);
    auto buf = make_buffer(roi, /*frame_w*/ 640, /*frame_h*/ 384);

    // Conversion-only mode: process() runs the conversion and short-circuits before
    // add_buffer_to_encoder because m_media_library is null. No DSP/DMA needed.
    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    auto mlib = buf->get_buffer();
    ASSERT_NE(mlib->m_analytics_metadata, nullptr);
    ASSERT_NE(mlib->m_analytics_metadata->m_semantic_segmentation, nullptr);
    const auto &masks = *mlib->m_analytics_metadata->m_semantic_segmentation;
    ASSERT_EQ(masks.size(), 1u);
    EXPECT_EQ(masks[0].label, "person");
    EXPECT_EQ(masks[0].mask.width, 16u);
    EXPECT_EQ(masks[0].mask.height, 16u);
    ASSERT_NE(masks[0].mask.mask, nullptr);
    EXPECT_EQ(masks[0].mask.mask_size, 16u * 16u);
    EXPECT_EQ(masks[0].mask.mask, mask_holder->data.data());
    EXPECT_EQ(mlib->m_analytics_metadata->m_detections, nullptr); // detection had a mask -> no overflow entry
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(WritesOverflowDetectionForUnmaskedDetection))
{
    auto stage = make_conversion_stage();

    auto roi = build_roi_with_unmasked_detection("person", HailoBBox(0.1f, 0.2f, 0.3f, 0.4f));
    auto buf = make_buffer(roi, /*frame_w*/ 640, /*frame_h*/ 384);

    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    auto mlib = buf->get_buffer();
    ASSERT_NE(mlib->m_analytics_metadata, nullptr);
    ASSERT_NE(mlib->m_analytics_metadata->m_detections, nullptr);
    const auto &detections = *mlib->m_analytics_metadata->m_detections;
    ASSERT_EQ(detections.size(), 1u);
    EXPECT_EQ(mlib->m_analytics_metadata->m_semantic_segmentation, nullptr);

    // HailoBBox stores (xmin, ymin, width, height), so xmax = xmin + width = 0.4, ymax = 0.6.
    // Coordinates are scaled by the BUFFER's dimensions (640x384 here), so each per-stream
    // EncoderStage produces wire-types in its own encoded-frame pixel space.
    const auto &d = detections[0].detection;
    EXPECT_FLOAT_EQ(d.x_min, 0.1f * 640.0f);
    EXPECT_FLOAT_EQ(d.y_min, 0.2f * 384.0f);
    EXPECT_FLOAT_EQ(d.x_max, 0.4f * 640.0f);
    EXPECT_FLOAT_EQ(d.y_max, 0.6f * 384.0f);
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(StampsDetectionLabelOnOverflowEntry))
{
    auto stage = make_conversion_stage();

    auto roi = build_roi_with_unmasked_detection("person", HailoBBox(0.1f, 0.2f, 0.3f, 0.4f));
    auto buf = make_buffer(roi, /*frame_w*/ 640, /*frame_h*/ 384);

    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    auto mlib = buf->get_buffer();
    ASSERT_NE(mlib->m_analytics_metadata, nullptr);
    ASSERT_NE(mlib->m_analytics_metadata->m_detections, nullptr);
    const auto &detections = *mlib->m_analytics_metadata->m_detections;
    ASSERT_EQ(detections.size(), 1u);
    EXPECT_EQ(detections[0].label, "person");
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(EmptyRoiYieldsAttachedButEmptyAnalyticsMetadata))
{
    auto stage = make_conversion_stage();

    auto buf = make_buffer(build_empty_roi(), 640, 384);

    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    // Always-attach: an empty ROI still produces a non-null AnalyticsMetadata carrier so that
    // null downstream means "AI did not run on this buffer", not "AI ran with no detections".
    // Both member vectors stay null because there's nothing to populate them with.
    auto mlib = buf->get_buffer();
    ASSERT_NE(mlib->m_analytics_metadata, nullptr);
    EXPECT_EQ(mlib->m_analytics_metadata->m_semantic_segmentation, nullptr);
    EXPECT_EQ(mlib->m_analytics_metadata->m_detections, nullptr);
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(EmitsOneWireTypePerClassMaskChildStampedWithDetectionLabel))
{
    // The encoder is now label-agnostic: it walks each detection's class_mask children and
    // emits one LabeledSemanticMask per child, stamped with the detection's label and the
    // child's index as class_id. Picking the right one per detection is the blender's job.
    auto stage = EncoderStageBuild::create()
                     .set_stage_name("test_per_child_emission")
                     .set_attach_analytics_metadata(true)
                     .buildptr();

    std::vector<std::shared_ptr<MaskHolder>> holders;
    auto roi = build_roi_with_multi_mask_detection("person", /*num_class_masks*/ 2,
                                                   /*mask_w*/ 16, /*mask_h*/ 16, holders);
    auto buf = make_buffer(roi, /*frame_w*/ 640, /*frame_h*/ 384);

    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    auto mlib = buf->get_buffer();
    ASSERT_NE(mlib->m_analytics_metadata, nullptr);
    ASSERT_NE(mlib->m_analytics_metadata->m_semantic_segmentation, nullptr);
    const auto &masks = *mlib->m_analytics_metadata->m_semantic_segmentation;

    // Two class_mask children → two wire-types.
    ASSERT_EQ(masks.size(), 2u);
    EXPECT_EQ(masks[0].label, "person");
    EXPECT_EQ(masks[0].mask.class_id, 0u);
    EXPECT_EQ(masks[1].label, "person");
    EXPECT_EQ(masks[1].mask.class_id, 1u);

    ASSERT_NE(masks[0].mask.mask, nullptr);
    ASSERT_NE(masks[1].mask.mask, nullptr);
    EXPECT_EQ(masks[0].mask.mask[0], 1u);
    EXPECT_EQ(masks[1].mask.mask[0], 2u);
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(EmitsForUnknownLabelsToo))
{
    // Encoder doesn't filter by label — that's the blender's job. A detection with an
    // arbitrary label and class_mask children still produces wire-types.
    auto stage =
        EncoderStageBuild::create().set_stage_name("test_unknown_label").set_attach_analytics_metadata(true).buildptr();

    std::vector<std::shared_ptr<MaskHolder>> holders;
    auto roi = build_roi_with_multi_mask_detection("airplane", /*num_class_masks*/ 2,
                                                   /*mask_w*/ 16, /*mask_h*/ 16, holders);
    auto buf = make_buffer(roi, /*frame_w*/ 640, /*frame_h*/ 384);

    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    auto mlib = buf->get_buffer();
    ASSERT_NE(mlib->m_analytics_metadata, nullptr);
    ASSERT_NE(mlib->m_analytics_metadata->m_semantic_segmentation, nullptr);
    const auto &masks = *mlib->m_analytics_metadata->m_semantic_segmentation;
    ASSERT_EQ(masks.size(), 2u);
    EXPECT_EQ(masks[0].label, "airplane");
    EXPECT_EQ(masks[1].label, "airplane");
    // The blender will subsequently drop these because "airplane" isn't in masked_labels.
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(AttachDisabledLeavesMetadataUntouched))
{
    auto stage =
        EncoderStageBuild::create().set_stage_name("test_attach_off").set_attach_analytics_metadata(false).buildptr();

    std::shared_ptr<MaskHolder> mask_holder;
    auto roi = build_roi_with_masked_detection("person", /*mask_w*/ 16, /*mask_h*/ 16, mask_holder);
    auto buf = make_buffer(roi, /*frame_w*/ 640, /*frame_h*/ 384);

    ASSERT_EQ(stage->process(buf), hailo_analytics::pipeline::AppStatus::SUCCESS);

    EXPECT_EQ(buf->get_buffer()->m_analytics_metadata, nullptr);
}

TEST(EncoderStageDpmConversion, PR_NIGHTLY_TIER(InitFailsWhenNeitherConfiguredNorAttaching))
{
    auto unconfigured = EncoderStageBuild::create()
                            .set_stage_name("test_init_unconfigured")
                            .set_attach_analytics_metadata(false)
                            .buildptr();
    EXPECT_EQ(unconfigured->init(), hailo_analytics::pipeline::AppStatus::UNINITIALIZED);

    auto conversion_only = EncoderStageBuild::create()
                               .set_stage_name("test_init_conversion_only")
                               .set_attach_analytics_metadata(true)
                               .buildptr();
    EXPECT_EQ(conversion_only->init(), hailo_analytics::pipeline::AppStatus::SUCCESS);
}
