#include <stddef.h>
#include <media_library/buffer_pool.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "core_tests_common.hpp" // pulls in `using namespace hailo_analytics::pipeline;` and SimpleStage
#include "hailo_analytics/pipeline/muxing/bundle_streams_stage.hpp"
#include "hailo_analytics/pipeline/muxing/split_streams_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "gtest/gtest.h"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

using namespace hailo_analytics::pipeline::muxing;

// Window the bundle/split processing thread is given to drain the input queues; matches the
// timing pattern established in stage_tests.cpp.
static constexpr auto kProcessingWaitMs = std::chrono::milliseconds(100);

namespace
{

BufferPtr make_buffer()
{
    return std::make_shared<Buffer>(std::make_shared<hailo_media_library_buffer>());
}

std::shared_ptr<BundleStreamsStage> make_bundle(const std::string &carrier, const std::vector<std::string> &passengers,
                                                size_t queue_size = 4)
{
    return BundleStreamsStageBuild::create()
        .set_stage_name("bundle")
        .set_carrier_stream_id(carrier)
        .set_passenger_stream_ids(passengers)
        .set_queue_size_opt(queue_size)
        .set_trace_opt(false)
        .buildptr();
}

std::shared_ptr<SplitStreamsStage> make_split(const std::string &carrier, bool propagate_roi = false)
{
    return SplitStreamsStageBuild::create()
        .set_stage_name("split")
        .set_carrier_stream_id(carrier)
        .set_propagate_roi_opt(propagate_roi)
        .set_queue_size_opt(4)
        .set_trace_opt(false)
        .buildptr();
}

// Builds a bundled buffer in the shape SplitStreamsStage expects: a carrier with one
// AttachedStreamMetadata per passenger.
BufferPtr make_bundled_carrier(const BufferPtr &carrier,
                               const std::vector<std::pair<std::string, BufferPtr>> &passengers)
{
    for (const auto &[stream_id, passenger] : passengers)
        carrier->add_metadata(std::make_shared<AttachedStreamMetadata>(passenger, stream_id));
    return carrier;
}

} // namespace

// ============================================================================
// BundleStreamsStage Builder & queue-validation tests
// ============================================================================

TEST(BundleStreamsStageBuilderTest, RejectsBuildWithoutCarrier)
{
    EXPECT_THROW(BundleStreamsStageBuild::create().set_stage_name("bundle").set_passenger_stream_ids({"a"}).buildptr(),
                 std::invalid_argument);
}

TEST(BundleStreamsStageBuilderTest, RejectsBuildWithEmptyPassengers)
{
    EXPECT_THROW(BundleStreamsStageBuild::create()
                     .set_stage_name("bundle")
                     .set_carrier_stream_id("ai")
                     .set_passenger_stream_ids({})
                     .buildptr(),
                 std::invalid_argument);
}

TEST(BundleStreamsStageBuilderTest, RejectsCarrierAppearingInPassengerList)
{
    EXPECT_THROW(BundleStreamsStageBuild::create()
                     .set_stage_name("bundle")
                     .set_carrier_stream_id("ai")
                     .set_passenger_stream_ids({"vision", "ai"})
                     .buildptr(),
                 std::invalid_argument);
}

TEST(BundleStreamsStageBuilderTest, RejectsDuplicatePassengerStreamIds)
{
    EXPECT_THROW(BundleStreamsStageBuild::create()
                     .set_stage_name("bundle")
                     .set_carrier_stream_id("ai")
                     .set_passenger_stream_ids({"vision", "vision"})
                     .buildptr(),
                 std::invalid_argument);
}

TEST(BundleStreamsStageTest, AddQueueRejectsUnrosteredPublisher)
{
    auto bundle = make_bundle("ai", {"vision"});
    EXPECT_THROW(bundle->add_queue("not_in_roster"), std::invalid_argument);
}

TEST(BundleStreamsStageTest, AddQueueAcceptsCarrierAndPassengerNames)
{
    auto bundle = make_bundle("ai", {"vision"});
    EXPECT_NO_THROW(bundle->add_queue("ai"));
    EXPECT_NO_THROW(bundle->add_queue("vision"));
}

// ============================================================================
// BundleStreamsStage runtime: bundling carrier + passengers
// ============================================================================

TEST(BundleStreamsStageTest, BundlesCarrierAndPassengerOntoSingleOutput)
{
    auto bundle = make_bundle("ai", {"vision"});
    auto sink = std::make_shared<SimpleStage>("sink");
    bundle->add_subscriber(sink);

    auto carrier = make_buffer();
    auto passenger = make_buffer();

    ASSERT_EQ(bundle->start(), AppStatus::SUCCESS);
    bundle->push(carrier, "ai");
    bundle->push(passenger, "vision");
    std::this_thread::sleep_for(kProcessingWaitMs);
    ASSERT_EQ(bundle->stop(), AppStatus::SUCCESS);

    ASSERT_EQ(sink->pushed_data.size(), 1u);
    EXPECT_EQ(sink->pushed_data[0].first, carrier);

    auto attached = carrier->get_metadata_of_type(MetadataType::ATTACHED_STREAM);
    ASSERT_EQ(attached.size(), 1u);
    auto stream_md = std::dynamic_pointer_cast<AttachedStreamMetadata>(attached[0]);
    ASSERT_NE(stream_md, nullptr);
    EXPECT_EQ(stream_md->get_stream_id(), "vision");
    EXPECT_EQ(stream_md->get_buffer(), passenger);
}

TEST(BundleStreamsStageTest, AttachesPassengerMetadataInRosterOrder)
{
    auto bundle = make_bundle("ai", {"vision_a", "vision_b", "vision_c"});
    auto sink = std::make_shared<SimpleStage>("sink");
    bundle->add_subscriber(sink);

    auto carrier = make_buffer();
    auto pa = make_buffer();
    auto pb = make_buffer();
    auto pc = make_buffer();

    ASSERT_EQ(bundle->start(), AppStatus::SUCCESS);
    bundle->push(carrier, "ai");
    bundle->push(pa, "vision_a");
    bundle->push(pb, "vision_b");
    bundle->push(pc, "vision_c");
    std::this_thread::sleep_for(kProcessingWaitMs);
    ASSERT_EQ(bundle->stop(), AppStatus::SUCCESS);

    auto attached = carrier->get_metadata_of_type(MetadataType::ATTACHED_STREAM);
    ASSERT_EQ(attached.size(), 3u);
    std::vector<std::string> order;
    for (const auto &md : attached)
        order.push_back(std::dynamic_pointer_cast<AttachedStreamMetadata>(md)->get_stream_id());
    EXPECT_EQ(order, (std::vector<std::string>{"vision_a", "vision_b", "vision_c"}));
}

// ============================================================================
// SplitStreamsStage init validation
// ============================================================================

TEST(SplitStreamsStageBuilderTest, RejectsBuildWithoutCarrier)
{
    EXPECT_THROW(SplitStreamsStageBuild::create().set_stage_name("split").buildptr(), std::invalid_argument);
}

TEST(SplitStreamsStageInitTest, FailsWhenNoSubscriberCarriesCarrierStreamId)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    split->add_subscriber(vision_sink, "vision"); // no subscriber for "ai"

    EXPECT_EQ(split->init(), AppStatus::INVALID_ARGUMENT);
}

TEST(SplitStreamsStageInitTest, FailsWhenSubscriberMissingStreamId)
{
    auto split = make_split("ai");
    auto sink = std::make_shared<SimpleStage>("sink");
    split->add_subscriber(sink); // no stream_id provided

    EXPECT_EQ(split->init(), AppStatus::INVALID_ARGUMENT);
}

TEST(SplitStreamsStageInitTest, BuildsRosterFromSubscribersWithMatchingCarrier)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");

    EXPECT_EQ(split->init(), AppStatus::SUCCESS);
}

// ============================================================================
// SplitStreamsStage process: routing, metadata stripping, ROI propagation
// ============================================================================

TEST(SplitStreamsStageProcessTest, DispatchesCarrierAndPassengersByStreamId)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    auto carrier = make_buffer();
    auto vision_passenger = make_buffer();
    make_bundled_carrier(carrier, {{"vision", vision_passenger}});

    EXPECT_EQ(split->process(carrier), AppStatus::SUCCESS);

    ASSERT_EQ(ai_sink->pushed_data.size(), 1u);
    EXPECT_EQ(ai_sink->pushed_data[0].first, carrier);
    ASSERT_EQ(vision_sink->pushed_data.size(), 1u);
    EXPECT_EQ(vision_sink->pushed_data[0].first, vision_passenger);
}

TEST(SplitStreamsStageProcessTest, StripsAttachedStreamMetadataFromCarrierBeforeDispatch)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    auto carrier = make_buffer();
    make_bundled_carrier(carrier, {{"vision", make_buffer()}});

    ASSERT_EQ(split->process(carrier), AppStatus::SUCCESS);
    EXPECT_TRUE(carrier->get_metadata_of_type(MetadataType::ATTACHED_STREAM).empty());
}

TEST(SplitStreamsStageProcessTest, PropagateRoiSharesCarrierRoiWithEveryPassenger)
{
    auto split = make_split("ai", /*propagate_roi=*/true);
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    auto carrier = make_buffer();
    auto passenger = make_buffer();
    auto carrier_roi = carrier->get_roi();
    ASSERT_NE(carrier_roi, nullptr);
    ASSERT_NE(passenger->get_roi(), carrier_roi); // sanity: distinct ROIs before split

    make_bundled_carrier(carrier, {{"vision", passenger}});
    ASSERT_EQ(split->process(carrier), AppStatus::SUCCESS);

    EXPECT_EQ(passenger->get_roi(), carrier_roi); // shallow share — same shared_ptr instance
}

TEST(SplitStreamsStageProcessTest, PropagateRoiDisabledLeavesPassengerRoiUntouched)
{
    auto split = make_split("ai", /*propagate_roi=*/false);
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    auto carrier = make_buffer();
    auto passenger = make_buffer();
    auto passenger_roi_before = passenger->get_roi();

    make_bundled_carrier(carrier, {{"vision", passenger}});
    ASSERT_EQ(split->process(carrier), AppStatus::SUCCESS);

    EXPECT_EQ(passenger->get_roi(), passenger_roi_before);
    EXPECT_NE(passenger->get_roi(), carrier->get_roi());
}

TEST(SplitStreamsStageProcessTest, RejectsBundleWithUnknownPassengerStreamId)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    auto carrier = make_buffer();
    make_bundled_carrier(carrier, {{"unknown_stream", make_buffer()}});

    EXPECT_EQ(split->process(carrier), AppStatus::INVALID_ARGUMENT);
}

TEST(SplitStreamsStageProcessTest, RejectsBundleMissingExpectedPassenger)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");
    split->add_subscriber(vision_sink, "vision");
    split->add_subscriber(ai_sink, "ai");
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    // Carrier with no AttachedStreamMetadata at all — vision passenger expected, none provided.
    auto carrier = make_buffer();
    EXPECT_EQ(split->process(carrier), AppStatus::INVALID_ARGUMENT);
}

// ============================================================================
// PipelineBuilder 3-arg connect: stream-id-keyed routing wired through build()
// ============================================================================

TEST(PipelineBuilderConnectTest, ThreeArgConnectRegistersStreamIdRouteOnSplit)
{
    auto split = make_split("ai");
    auto vision_sink = std::make_shared<SimpleStage>("vision_sink");
    auto ai_sink = std::make_shared<SimpleStage>("ai_sink");

    PipelineBuilder()
        .add_stage("split", split)
        .add_stage("vision_sink", vision_sink, hailo_analytics::pipeline::StageType::SINK)
        .add_stage("ai_sink", ai_sink, hailo_analytics::pipeline::StageType::SINK)
        .connect("split", "vision", "vision_sink")
        .connect("split", "ai", "ai_sink")
        .build("test_pipeline", false);

    // Build wires the subscribers via add_subscriber(target, streamId). init() walks the
    // registered subscriber stream_ids; if connect()'s streamId didn't ride through, init fails.
    ASSERT_EQ(split->init(), AppStatus::SUCCESS);

    auto carrier = make_buffer();
    auto vision_passenger = make_buffer();
    make_bundled_carrier(carrier, {{"vision", vision_passenger}});
    ASSERT_EQ(split->process(carrier), AppStatus::SUCCESS);

    ASSERT_EQ(vision_sink->pushed_data.size(), 1u);
    EXPECT_EQ(vision_sink->pushed_data[0].first, vision_passenger);
    ASSERT_EQ(ai_sink->pushed_data.size(), 1u);
    EXPECT_EQ(ai_sink->pushed_data[0].first, carrier);
}
