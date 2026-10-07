// general includes
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>
#include <stdint.h>
#include <stdlib.h>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "media_library_factory.hpp"
// medialibrary includes
#include "media_library/signal_utils.hpp"
// infra includes
#include "face_landmarks_pipeline_builder.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/analytics/analytic_metadata_zmq_sender.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/utils/platform_utils.hpp"
#include "hailo_analytics/utils/profile_utils.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

using hailo_analytics::pipeline::AppStatus;

// Pipeline names
static constexpr const char *SENSOR0_VISION_PIPELINE = "sensor0_vision_pipeline";
static constexpr const char *SENSOR1_VISION_PIPELINE = "sensor1_vision_pipeline";
static constexpr const char *TILING_PIPELINE = "tiling_pipeline";
static constexpr const char *LANDMARKS_PIPELINE = "landmarks_pipeline";
static constexpr const char *ANALYTIC_META_SENDER_PIPELINE = "analytic_metadata_sender_pipeline";
static constexpr const char *APP_NAME = "face_landmarks_dual_sensor_app";

// Stream IDs
static constexpr const char *AI_SINK = "sink2";

// Default config paths
static const std::string HOST_IP = hailo_analytics::analytics::vision::get_default_host_ip();
static constexpr const char *NO_PROFILE_SELECTED = "";
static constexpr const char *MEDIALIB_CONFIG_PATH_SENSOR_0 =
    "/etc/imaging/cfg/medialib_configs/face_landmarks_dual_sensor_medialib_config_sensor_0.json";
static constexpr const char *MEDIALIB_CONFIG_PATH_SENSOR_1 =
    "/etc/imaging/cfg/medialib_configs/face_landmarks_dual_sensor_medialib_config_sensor_1.json";

// Port allocation
static constexpr int SENSOR0_BASE_PORT = 5000;
static constexpr int SENSOR1_BASE_PORT = 5100;

static constexpr uint32_t TARGET_AI_FRAMERATE = 15;

enum class ArgumentType
{
    Help,
    Timeout,
    ConfigSensor0,
    ConfigSensor1,
    ProfileSensor0,
    ProfileSensor1,
    HostIP,
    UdpPortSensor0,
    UdpPortSensor1,
    ZmqPort,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("Face landmarks dual sensor app");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run",
        cxxopts::value<int>()->default_value("60"))
    ("c,config-file-path-sensor-0", "Media library configuration path for sensor 0 (AI pipeline)",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH_SENSOR_0))
    ("d,config-file-path-sensor-1", "Media library configuration path for sensor 1 (simple stream)",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH_SENSOR_1))
    ("a,profile-sensor-0", "Profile name for sensor 0",
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("b,profile-sensor-1", "Profile name for sensor 1",
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("o,host-ip", "Host IP address for UDP output",
        cxxopts::value<std::string>()->default_value(HOST_IP))
    ("u,udp-port-sensor-0", "Base UDP port for sensor 0",
        cxxopts::value<std::string>()->default_value(""))
    ("v,udp-port-sensor-1", "Base UDP port for sensor 1",
        cxxopts::value<std::string>()->default_value(""))
    ("z,zmq-port", "ZMQ publisher port (default: 7000)",
        cxxopts::value<std::string>()->default_value(""));
    // clang-format on

    options.add_options()(apps::utils::USE_MEDIALIB_SERVICE_OPTION,
                          "Connect to media_library_service instead of hosting the media library in this process",
                          cxxopts::value<bool>()->default_value("false"));

    return options;
}

std::vector<ArgumentType> handle_arguments(const cxxopts::ParseResult &result, const cxxopts::Options &options)
{
    std::vector<ArgumentType> arguments;

    if (result.count("help"))
    {
        print_help(options);
        arguments.push_back(ArgumentType::Help);
    }

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("config-file-path-sensor-0"))
    {
        arguments.push_back(ArgumentType::ConfigSensor0);
    }

    if (result.count("config-file-path-sensor-1"))
    {
        arguments.push_back(ArgumentType::ConfigSensor1);
    }

    if (result.count("profile-sensor-0"))
    {
        arguments.push_back(ArgumentType::ProfileSensor0);
    }

    if (result.count("profile-sensor-1"))
    {
        arguments.push_back(ArgumentType::ProfileSensor1);
    }

    if (result.count("host-ip"))
    {
        arguments.push_back(ArgumentType::HostIP);
    }

    if (result.count("udp-port-sensor-0") && !result["udp-port-sensor-0"].as<std::string>().empty())
    {
        arguments.push_back(ArgumentType::UdpPortSensor0);
    }

    if (result.count("udp-port-sensor-1") && !result["udp-port-sensor-1"].as<std::string>().empty())
    {
        arguments.push_back(ArgumentType::UdpPortSensor1);
    }

    if (result.count("zmq-port") && !result["zmq-port"].as<std::string>().empty())
    {
        arguments.push_back(ArgumentType::ZmqPort);
    }

    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}

struct MediaLibraryInstance
{
    MediaLibraryInterfacePtr media_library;
    std::string medialib_config_path;
    std::string profile_name;
};

struct AppResources
{
    static constexpr int NUM_SENSORS = 2;

    std::vector<MediaLibraryInstance> instances;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    std::string host_ip = HOST_IP;
    int sensor0_base_port = SENSOR0_BASE_PORT;
    int sensor1_base_port = SENSOR1_BASE_PORT;
    std::string zmq_port;
    bool use_medialib_service = false;

    AppResources()
    {
        instances.resize(NUM_SENSORS);
    }
};

AppStatus configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    // Currently, a two-sensor app does not support working in client-service mode.
    if (app_resources->use_medialib_service)
    {
        std::cerr << "Error: "
                  << apps::utils::service_mode_unsupported_message(
                         "it drives " + std::to_string(AppResources::NUM_SENSORS) +
                         " sensors, and media_library_service serves a single media library")
                  << std::endl;
        return AppStatus::INVALID_ARGUMENT;
    }

    for (int i = 0; i < AppResources::NUM_SENSORS; ++i)
    {
        auto media_library = apps::utils::create_media_library(app_resources->use_medialib_service,
                                                               app_resources->instances[i].medialib_config_path);
        if (!media_library.has_value())
        {
            return AppStatus::MEDIA_LIBRARY_ERROR;
        }
        app_resources->instances[i].media_library = media_library.value();
        if (app_resources->instances[i].profile_name != NO_PROFILE_SELECTED)
        {
            hailo_analytics::utils::set_initial_profile(app_resources->instances[i].media_library,
                                                        app_resources->instances[i].profile_name);
        }
    }
    return AppStatus::SUCCESS;
}

// Crop divisor that holds AI inference at ~TARGET_AI_FRAMERATE for the AI_SINK stream.
static size_t ai_crop_every_x_frames(const std::vector<frontend_output_stream_t> &streams)
{
    for (const auto &stream : streams)
    {
        if (stream.id == AI_SINK)
        {
            return std::max<size_t>(1, stream.target_fps / TARGET_AI_FRAMERATE);
        }
    }
    HAILO_ANALYTICS_LOG_ERROR("Could not find AI_SINK stream '{}' among frontend output streams", AI_SINK);
    return 1;
}

/* Sensor 0 = face landmarks AI pipeline (vision -> tiling -> landmarks -> ZMQ sender). */
static AppStatus build_sensor0_ai_chain(hailo_analytics::pipeline::PipelineBuilder &pip_builder,
                                        std::shared_ptr<AppResources> app_resources)
{
    auto sensor0_media_library = app_resources->instances[0].media_library;
    auto sensor0_streams = sensor0_media_library->get_frontend_output_streams();
    if (!sensor0_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get output streams for sensor 0");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    // Vision pipeline for sensor 0 — erase AI_SINK so it's not auto-encoded
    auto sensor0_vision_config = hailo_analytics::analytics::vision::base_vision_config(
        sensor0_streams.value(), app_resources->sensor0_base_port);
    sensor0_vision_config.outputs.erase(AI_SINK);
    for (auto &[stream_id, output_config] : sensor0_vision_config.outputs)
    {
        output_config.udp_config.host = app_resources->host_ip;
    }

    auto sensor0_vision_pipeline = hailo_analytics::analytics::vision::generate_vision_pipeline(
        sensor0_media_library, SENSOR0_VISION_PIPELINE, sensor0_vision_config);
    if (!sensor0_vision_pipeline.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline for sensor 0");
        return sensor0_vision_pipeline.error();
    }

    // Both the detection and landmarks crop stages run on the AI_SINK stream, so each
    // downsamples to ~TARGET_AI_FRAMERATE by the same divisor.
    const size_t crop_every_x_frames = ai_crop_every_x_frames(sensor0_streams.value());

    // Tiling pipeline (YOLO person/face detection)
    auto tiling_cfg = face_landmarks_app::default_tiling_config();
    tiling_cfg.tiling_config.crop_every_x_frames = crop_every_x_frames;
    auto tiling_pipeline =
        hailo_analytics::analytics::tiling::generate_tiling_detection_pipeline(TILING_PIPELINE, tiling_cfg);
    if (!tiling_pipeline.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create tiling pipeline");
        return tiling_pipeline.error();
    }

    // Face landmarks pipeline
    auto current_profile = sensor0_media_library->get_current_profile();
    if (!current_profile.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get current profile");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }
    auto landmarks_cfg = face_landmarks_app::default_landmarks_config(current_profile.value());
    landmarks_cfg.bbox_crop_config.crop_every_x_frames = crop_every_x_frames;
    auto landmarks_pipeline =
        hailo_analytics::analytics::face_landmarks::generate_bbox_landmarks_pipeline(LANDMARKS_PIPELINE, landmarks_cfg);
    if (!landmarks_pipeline.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create landmarks pipeline");
        return landmarks_pipeline.error();
    }

    // Analytic metadata ZMQ sender pipeline
    hailo_analytics::analytics::analytic_metadata_zmq_sender::analytic_metadata_zmq_sender_config_t sender_config;
    sender_config.analytic_metadata_config.queue_size = 1;
    sender_config.zeromq_config.queue_size = 1;
    if (!app_resources->zmq_port.empty())
    {
        sender_config.zeromq_config.pub_address = "tcp://*:" + app_resources->zmq_port;
    }
    auto metadata_sender_pipeline =
        hailo_analytics::analytics::analytic_metadata_zmq_sender::generate_analytic_metadata_zmq_sender_pipeline(
            ANALYTIC_META_SENDER_PIPELINE, sender_config);
    if (!metadata_sender_pipeline.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create analytic metadata sender pipeline");
        return metadata_sender_pipeline.error();
    }

    pip_builder.add_stage(sensor0_vision_pipeline.value(), hailo_analytics::pipeline::StageType::SOURCE)
        .add_stage(tiling_pipeline.value())
        .add_stage(landmarks_pipeline.value())
        .add_stage(metadata_sender_pipeline.value(), hailo_analytics::pipeline::StageType::SINK);

    // Connect sensor 0 AI chain: vision[AI_SINK] -> tiling -> landmarks -> zmq_sender
    pip_builder.connect_frontend(SENSOR0_VISION_PIPELINE, AI_SINK, TILING_PIPELINE);
    pip_builder.connect(TILING_PIPELINE, LANDMARKS_PIPELINE);
    pip_builder.connect(LANDMARKS_PIPELINE, ANALYTIC_META_SENDER_PIPELINE);

    return AppStatus::SUCCESS;
}

/* Sensor 1 = simple vision pipeline (all streams encoded to UDP, no AI). */
static AppStatus build_sensor1_stream_chain(hailo_analytics::pipeline::PipelineBuilder &pip_builder,
                                            std::shared_ptr<AppResources> app_resources)
{
    auto sensor1_media_library = app_resources->instances[1].media_library;
    auto sensor1_streams = sensor1_media_library->get_frontend_output_streams();
    if (!sensor1_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get output streams for sensor 1");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    auto sensor1_vision_config = hailo_analytics::analytics::vision::base_vision_config(
        sensor1_streams.value(), app_resources->sensor1_base_port);
    for (auto &[stream_id, output_config] : sensor1_vision_config.outputs)
    {
        output_config.udp_config.host = app_resources->host_ip;
    }

    auto sensor1_vision_pipeline = hailo_analytics::analytics::vision::generate_vision_pipeline(
        sensor1_media_library, SENSOR1_VISION_PIPELINE, sensor1_vision_config);
    if (!sensor1_vision_pipeline.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline for sensor 1");
        return sensor1_vision_pipeline.error();
    }

    pip_builder.add_stage(sensor1_vision_pipeline.value(), hailo_analytics::pipeline::StageType::SOURCE);

    return AppStatus::SUCCESS;
}

AppStatus create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    AppStatus sensor0_status = build_sensor0_ai_chain(pip_builder, app_resources);
    if (sensor0_status != AppStatus::SUCCESS)
    {
        return sensor0_status;
    }
    AppStatus sensor1_status = build_sensor1_stream_chain(pip_builder, app_resources);
    if (sensor1_status != AppStatus::SUCCESS)
    {
        return sensor1_status;
    }

    app_resources->pipeline = pip_builder.build(APP_NAME, true);

    return AppStatus::SUCCESS;
}

std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;

int main(int argc, char *argv[])
{
    auto app_resources = std::make_shared<AppResources>();
    app_resources->instances[0].medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR_0;
    app_resources->instances[1].medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR_1;

    // Register signal handler for graceful shutdown
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([]([[maybe_unused]] int signal) {
        std::cout << "Stopping Pipeline..." << std::endl;
        HAILO_ANALYTICS_LOG_INFO("Stopping Pipeline...");
        g_stop_cv.notify_all();
    });

    // Parse user arguments
    cxxopts::Options options = build_arg_parser();
    auto result = options.parse(argc, argv);
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options);
    int timeout = result["timeout"].as<int>();

    for (ArgumentType argument : argument_handling_results)
    {
        switch (argument)
        {
        case ArgumentType::Help:
            return 0;
        case ArgumentType::Timeout:
            break;
        case ArgumentType::ConfigSensor0:
            app_resources->instances[0].medialib_config_path = result["config-file-path-sensor-0"].as<std::string>();
            break;
        case ArgumentType::ConfigSensor1:
            app_resources->instances[1].medialib_config_path = result["config-file-path-sensor-1"].as<std::string>();
            break;
        case ArgumentType::ProfileSensor0:
            app_resources->instances[0].profile_name = result["profile-sensor-0"].as<std::string>();
            break;
        case ArgumentType::ProfileSensor1:
            app_resources->instances[1].profile_name = result["profile-sensor-1"].as<std::string>();
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::UdpPortSensor0:
            app_resources->sensor0_base_port = std::stoi(result["udp-port-sensor-0"].as<std::string>());
            break;
        case ArgumentType::UdpPortSensor1:
            app_resources->sensor1_base_port = std::stoi(result["udp-port-sensor-1"].as<std::string>());
            break;
        case ArgumentType::ZmqPort:
            app_resources->zmq_port = result["zmq-port"].as<std::string>();
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    // Required for dual sensor framerate logic
    setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);

    app_resources->use_medialib_service = result[apps::utils::USE_MEDIALIB_SERVICE_OPTION].as<bool>();

    // This app is dual-sensor only: refuse to run unless every sensor is connected.
    if (!hailo_analytics::utils::validate_all_sensors_are_present(AppResources::NUM_SENSORS))
    {
        std::string message = "This app requires " + std::to_string(AppResources::NUM_SENSORS) +
                              " sensors, not all are currently connected";
        std::cerr << "Error: " << message << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("{}", message);
        return 1;
    }

    if (configure_media_library(app_resources) != AppStatus::SUCCESS)
    {
        return 1;
    }
    if (create_pipeline(app_resources) != AppStatus::SUCCESS)
    {
        return 1;
    }

    // Start pipeline
    std::cout << "Starting." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Starting.");
    app_resources->pipeline->start();

    HAILO_ANALYTICS_LOG_INFO("Started playing for {} seconds.", timeout);

    // Wait for either timeout or signal
    std::unique_lock<std::mutex> lk(g_stop_mutex);
    g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));

    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Stopping.");
    app_resources->pipeline->stop();
    return 0;
}
