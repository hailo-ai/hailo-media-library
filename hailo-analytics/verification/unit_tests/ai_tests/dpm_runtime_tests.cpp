#include <stdint.h>
#include <media_library/buffer_pool.hpp>
#include <media_library/media_library_buffer.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "test_tier.hpp"
#include "gtest/gtest.h"

namespace
{

namespace dpm = hailo_analytics::analytics::dpm_analytics;
namespace pipeline = hailo_analytics::pipeline;

constexpr uint32_t FRAME_W = 1920;
constexpr uint32_t FRAME_H = 1080;

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

} // namespace

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
