#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

#include "hailo_analytics/pipeline/sinks/websocket_sink_stage.hpp"
#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

using namespace hailo_analytics::pipeline::sinks;
using namespace hailo_analytics::pipeline;

namespace
{

constexpr uint16_t TEST_PORT_BASE = 19100;
std::atomic<uint16_t> g_port_counter{0};

uint16_t next_test_port()
{
    return TEST_PORT_BASE + g_port_counter++;
}

HailoMediaLibraryBufferPtr make_mock_buffer()
{
    auto buf = std::make_shared<hailo_media_library_buffer>();
    buf->buffer_data = std::make_shared<hailo_buffer_data_t>(1920, 1080, 0, HailoFormat{}, HailoMemoryType{},
                                                             std::vector<hailo_data_plane_t>{});
    buf->isp_timestamp_ns = 100;
    return buf;
}

void attach_zmq_json_message(BufferPtr buffer)
{
    nlohmann::json metadata_json = codecs::build_metadata_json(buffer);
    auto zmq_msg = std::make_shared<HailoZMQMessage>();
    zmq_msg->set_output_msg(metadata_json.dump());
    buffer->get_roi()->add_object(zmq_msg);
}

BufferPtr make_buffer_with_detection()
{
    auto mock_buf = make_mock_buffer();
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    roi->add_object(std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.2f, 0.3f, 0.4f), "person", 0.9f));
    auto buffer = std::make_shared<Buffer>(mock_buf, roi);
    attach_zmq_json_message(buffer);
    return buffer;
}

BufferPtr make_buffer_with_face_landmarks()
{
    auto mock_buf = make_mock_buffer();
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));

    auto person = std::make_shared<HailoDetection>(HailoBBox(0.0f, 0.0f, 0.5f, 0.8f), "person", 0.95f);
    auto face = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.0f, 0.4f, 0.3f), "face", 0.88f);

    std::vector<HailoPoint> points = {HailoPoint(0.3f, 0.4f, 0.99f), HailoPoint(0.7f, 0.4f, 0.98f),
                                      HailoPoint(0.5f, 0.6f, 0.97f)};
    std::vector<std::pair<int, int>> pairs = {{0, 1}, {1, 2}};
    face->add_object(std::make_shared<HailoLandmarks>("face_landmarks_98", points, 0.5f, pairs));
    person->add_object(face);
    roi->add_object(person);

    auto buffer = std::make_shared<Buffer>(mock_buf, roi);
    attach_zmq_json_message(buffer);
    return buffer;
}

BufferPtr make_empty_buffer()
{
    return std::make_shared<Buffer>(make_mock_buffer());
}

} // namespace

TEST(WebSocketSinkStageBuildTest, BuilderCreatesStageWithName)
{
    auto stage = WebSocketSinkStageBuild::create().set_stage_name("test_sink").buildptr();
    ASSERT_NE(stage, nullptr);
}

TEST(WebSocketSinkStageBuildTest, BuilderThrowsWhenNameMissing)
{
    EXPECT_THROW(WebSocketSinkStageBuild::create().buildptr(), std::runtime_error);
}

TEST(WebSocketSinkStageBuildTest, BuilderChainingReturnsReference)
{
    auto builder = WebSocketSinkStageBuild::create().set_stage_name("chain_test");
    auto &builder2 = builder.set_port_opt(8888);
    auto &builder3 = builder2.set_queue_size_opt(3);
    auto &builder4 = builder3.set_leaky_opt(true);
    auto stage = builder4.buildptr();
    ASSERT_NE(stage, nullptr);
}

TEST(MetadataWebSocketSinkStageTest, InitSucceeds)
{
    auto stage =
        WebSocketSinkStageBuild::create().set_stage_name("init_test").set_port_opt(next_test_port()).buildptr();
    EXPECT_EQ(stage->init(), AppStatus::SUCCESS);
    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, DeinitWithoutInit)
{
    auto stage =
        WebSocketSinkStageBuild::create().set_stage_name("no_init_deinit").set_port_opt(next_test_port()).buildptr();
    EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS);
}

TEST(MetadataWebSocketSinkStageTest, InitDeinitMultipleCycles)
{
    uint16_t port = next_test_port();
    auto stage = WebSocketSinkStageBuild::create().set_stage_name("multi_cycle").set_port_opt(port).buildptr();

    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(stage->init(), AppStatus::SUCCESS) << "Cycle " << i;
        EXPECT_EQ(stage->deinit(), AppStatus::SUCCESS) << "Cycle " << i;
    }
}

TEST(MetadataWebSocketSinkStageTest, ProcessWithNoClientsReturnsSuccess)
{
    auto stage =
        WebSocketSinkStageBuild::create().set_stage_name("no_clients").set_port_opt(next_test_port()).buildptr();
    stage->init();

    auto buffer = make_buffer_with_detection();
    EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);

    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, ClientConnectAndReceiveDetection)
{
    uint16_t port = next_test_port();
    auto stage = WebSocketSinkStageBuild::create().set_stage_name("client_test").set_port_opt(port).buildptr();
    stage->init();

    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();

    auto client = std::make_shared<rtc::WebSocket>();
    client->onMessage([&](rtc::message_variant msg) {
        if (std::holds_alternative<std::string>(msg))
        {
            try
            {
                received_promise.set_value(std::get<std::string>(msg));
            }
            catch (...)
            {
            }
        }
    });
    client->open("ws://127.0.0.1:" + std::to_string(port));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    stage->process(make_buffer_with_detection());

    auto status = received_future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    auto json = nlohmann::json::parse(received_future.get());
    EXPECT_TRUE(json.contains(analytic_metadata_fields::DETECTIONS));
    EXPECT_EQ(json[analytic_metadata_fields::DETECTIONS][0][analytic_metadata_fields::detection::LABEL], "person");

    client->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, ClientReceiveFaceLandmarksData)
{
    uint16_t port = next_test_port();
    auto stage = WebSocketSinkStageBuild::create().set_stage_name("landmarks_test").set_port_opt(port).buildptr();
    stage->init();

    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();

    auto client = std::make_shared<rtc::WebSocket>();
    client->onMessage([&](rtc::message_variant msg) {
        if (std::holds_alternative<std::string>(msg))
        {
            try
            {
                received_promise.set_value(std::get<std::string>(msg));
            }
            catch (...)
            {
            }
        }
    });
    client->open("ws://127.0.0.1:" + std::to_string(port));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    stage->process(make_buffer_with_face_landmarks());

    auto status = received_future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    auto json = nlohmann::json::parse(received_future.get());

    // Top-level: person detection
    ASSERT_TRUE(json.contains(analytic_metadata_fields::DETECTIONS));
    auto &person = json[analytic_metadata_fields::DETECTIONS][0];
    EXPECT_EQ(person[analytic_metadata_fields::detection::LABEL], "person");

    // Nested: face detection under person
    ASSERT_TRUE(person.contains(analytic_metadata_fields::DETECTIONS));
    auto &face = person[analytic_metadata_fields::DETECTIONS][0];
    EXPECT_EQ(face[analytic_metadata_fields::detection::LABEL], "face");

    // Nested: landmarks under face
    ASSERT_TRUE(face.contains(analytic_metadata_fields::LANDMARKS));
    auto &landmarks = face[analytic_metadata_fields::LANDMARKS];
    ASSERT_EQ(landmarks.size(), 1);

    // 3 points * 3 values (x, y, confidence) = 9 floats
    auto &pts = landmarks[0][analytic_metadata_fields::landmark::POINTS];
    ASSERT_EQ(pts.size(), 9);

    // 2 pairs * 2 indices = 4 ints
    auto &pairs = landmarks[0][analytic_metadata_fields::landmark::PAIRS];
    ASSERT_EQ(pairs.size(), 4);

    client->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, ClientReceiveLargePayloadWithManyFaces)
{
    static constexpr int NUM_FACES = 50;
    static constexpr int LANDMARKS_PER_FACE = 98;

    uint16_t port = next_test_port();
    auto stage = WebSocketSinkStageBuild::create().set_stage_name("large_payload_test").set_port_opt(port).buildptr();
    stage->init();

    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();
    std::promise<void> open_promise;
    auto open_future = open_promise.get_future();

    rtc::WebSocket::Configuration client_config;
    client_config.maxMessageSize = 1024 * 1024; // 1 MB to match server
    auto client = std::make_shared<rtc::WebSocket>(client_config);
    client->onOpen([&]() {
        try
        {
            open_promise.set_value();
        }
        catch (...)
        {
        }
    });
    client->onMessage([&](rtc::message_variant msg) {
        if (std::holds_alternative<std::string>(msg))
        {
            try
            {
                received_promise.set_value(std::get<std::string>(msg));
            }
            catch (...)
            {
            }
        }
    });
    client->open("ws://127.0.0.1:" + std::to_string(port));
    ASSERT_EQ(open_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Build a buffer with NUM_FACES faces, each containing LANDMARKS_PER_FACE landmark points
    auto mock_buf = make_mock_buffer();
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    for (int i = 0; i < NUM_FACES; i++)
    {
        float x_offset = static_cast<float>(i % 10) * 0.1f;
        float y_offset = static_cast<float>(i / 10) * 0.2f;
        auto person = std::make_shared<HailoDetection>(HailoBBox(x_offset, y_offset, 0.08f, 0.15f), "person", 0.9f);
        auto face = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.0f, 0.6f, 0.4f), "face", 0.85f);

        std::vector<HailoPoint> points;
        points.reserve(LANDMARKS_PER_FACE);
        for (int j = 0; j < LANDMARKS_PER_FACE; j++)
        {
            float frac = static_cast<float>(j) / static_cast<float>(LANDMARKS_PER_FACE);
            points.emplace_back(frac, 1.0f - frac, 0.95f);
        }
        std::vector<std::pair<int, int>> pairs;
        for (int j = 0; j < LANDMARKS_PER_FACE - 1; j++)
            pairs.emplace_back(j, j + 1);

        face->add_object(std::make_shared<HailoLandmarks>("face_landmarks_98", points, 0.5f, pairs));
        person->add_object(face);
        roi->add_object(person);
    }

    auto buffer = std::make_shared<Buffer>(mock_buf, roi);
    attach_zmq_json_message(buffer);

    stage->process(buffer);

    auto status = received_future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "Large payload with " << NUM_FACES << " faces was not received";

    auto json = nlohmann::json::parse(received_future.get());
    ASSERT_TRUE(json.contains(analytic_metadata_fields::DETECTIONS));
    EXPECT_EQ(json[analytic_metadata_fields::DETECTIONS].size(), NUM_FACES);

    client->close();
    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, SmallMaxMessageSizeRejectsLargePayload)
{
    uint16_t port = next_test_port();
    static constexpr size_t TINY_LIMIT = 512; // 512 bytes — too small for any detection JSON
    auto stage = WebSocketSinkStageBuild::create()
                     .set_stage_name("small_limit_test")
                     .set_port_opt(port)
                     .set_max_message_size_opt(TINY_LIMIT)
                     .buildptr();
    stage->init();

    std::promise<void> open_promise;
    auto open_future = open_promise.get_future();
    std::atomic<bool> message_received{false};

    auto client = std::make_shared<rtc::WebSocket>();
    client->onOpen([&]() {
        try
        {
            open_promise.set_value();
        }
        catch (...)
        {
        }
    });
    client->onMessage([&](rtc::message_variant msg) { message_received.store(true); });
    client->open("ws://127.0.0.1:" + std::to_string(port));
    ASSERT_EQ(open_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // Process a detection buffer — its JSON will exceed the tiny 512-byte limit
    auto buffer = make_buffer_with_face_landmarks();
    EXPECT_EQ(stage->process(buffer), AppStatus::SUCCESS);

    // Brief wait to confirm no message arrives (negative test — some wait is unavoidable)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(message_received.load()) << "Message should have been rejected due to size limit";

    client->close();
    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, CustomMaxMessageSizeAllowsLargePayload)
{
    static constexpr int NUM_FACES = 50;
    static constexpr int LANDMARKS_PER_FACE = 98;
    static constexpr size_t LARGE_LIMIT = 2 * 1024 * 1024; // 2 MB

    uint16_t port = next_test_port();
    auto stage = WebSocketSinkStageBuild::create()
                     .set_stage_name("custom_limit_test")
                     .set_port_opt(port)
                     .set_max_message_size_opt(LARGE_LIMIT)
                     .buildptr();
    stage->init();

    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();
    std::promise<void> open_promise;
    auto open_future = open_promise.get_future();

    rtc::WebSocket::Configuration client_config;
    client_config.maxMessageSize = LARGE_LIMIT;
    auto client = std::make_shared<rtc::WebSocket>(client_config);
    client->onOpen([&]() {
        try
        {
            open_promise.set_value();
        }
        catch (...)
        {
        }
    });
    client->onMessage([&](rtc::message_variant msg) {
        if (std::holds_alternative<std::string>(msg))
        {
            try
            {
                received_promise.set_value(std::get<std::string>(msg));
            }
            catch (...)
            {
            }
        }
    });
    client->open("ws://127.0.0.1:" + std::to_string(port));
    ASSERT_EQ(open_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto mock_buf = make_mock_buffer();
    auto roi = std::make_shared<HailoROI>(HailoBBox(0, 0, 1, 1));
    for (int i = 0; i < NUM_FACES; i++)
    {
        float x_offset = static_cast<float>(i % 10) * 0.1f;
        float y_offset = static_cast<float>(i / 10) * 0.2f;
        auto person = std::make_shared<HailoDetection>(HailoBBox(x_offset, y_offset, 0.08f, 0.15f), "person", 0.9f);
        auto face = std::make_shared<HailoDetection>(HailoBBox(0.1f, 0.0f, 0.6f, 0.4f), "face", 0.85f);

        std::vector<HailoPoint> points;
        points.reserve(LANDMARKS_PER_FACE);
        for (int j = 0; j < LANDMARKS_PER_FACE; j++)
        {
            float frac = static_cast<float>(j) / static_cast<float>(LANDMARKS_PER_FACE);
            points.emplace_back(frac, 1.0f - frac, 0.95f);
        }
        std::vector<std::pair<int, int>> pairs;
        for (int j = 0; j < LANDMARKS_PER_FACE - 1; j++)
            pairs.emplace_back(j, j + 1);

        face->add_object(std::make_shared<HailoLandmarks>("face_landmarks_98", points, 0.5f, pairs));
        person->add_object(face);
        roi->add_object(person);
    }

    auto buffer = std::make_shared<Buffer>(mock_buf, roi);
    attach_zmq_json_message(buffer);

    stage->process(buffer);

    auto status = received_future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "Large payload should be accepted with custom 2MB limit";

    auto json = nlohmann::json::parse(received_future.get());
    ASSERT_TRUE(json.contains(analytic_metadata_fields::DETECTIONS));
    EXPECT_EQ(json[analytic_metadata_fields::DETECTIONS].size(), NUM_FACES);

    client->close();
    stage->deinit();
}

TEST(MetadataWebSocketSinkStageTest, ClientDisconnectStopsReceiving)
{
    uint16_t port = next_test_port();
    auto stage = WebSocketSinkStageBuild::create().set_stage_name("disconnect_test").set_port_opt(port).buildptr();
    stage->init();

    auto client = std::make_shared<rtc::WebSocket>();
    std::promise<void> connected_promise;
    auto connected_future = connected_promise.get_future();
    client->onOpen([&]() {
        try
        {
            connected_promise.set_value();
        }
        catch (...)
        {
        }
    });
    client->open("ws://127.0.0.1:" + std::to_string(port));
    connected_future.wait_for(std::chrono::seconds(2));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Process after disconnect should succeed (no crash, no clients to send to)
    EXPECT_EQ(stage->process(make_buffer_with_detection()), AppStatus::SUCCESS);

    stage->deinit();
}
