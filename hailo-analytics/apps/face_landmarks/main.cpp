// general includes
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>
#include <stddef.h>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "media_library/cloexec_fstream.hpp"
// medialibrary includes
#include "media_library/signal_utils.hpp"
// infra includes
#include "face_landmarks_pipeline_builder.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/analytics/analytic_metadata_zmq_sender.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/analytics/face_landmarks.hpp"
#include "hailo_analytics/analytics/tiling.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"

// defines
#define VISION_PIPELINE "vision_pipeline"
#define TILING_PIPELINE "tiling_pipeline"
#define LANDMARKS_PIPELINE "landmarks_pipeline"
#define ANALYTIC_META_SENDER_PIPELINE "analytic_metadata_sender_pipeline"
#define APP_NAME "face_landmarks_app"
#define HOST_IP "10.0.0.2"
#define VISION_SINK "sink0"
#define AI_SINK "sink2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/face_landmarks_medialib_config.json"

enum class ArgumentType
{
    Help,
    PrintFPS,
    Timeout,
    Config,
    Profile,
    HostIP,
    UdpPort,
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
    cxxopts::Options options("AI pipeline app");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run", 
        cxxopts::value<int>()->default_value("60"))
    ("p,print-fps", "Print FPS", 
        cxxopts::value<bool>()->default_value("false"))
    ("c,config-file-path", "Media library configuration path", 
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("a,profile", "Profile name", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("o,host-ip", "Host IP address for UDP output",
        cxxopts::value<std::string>()->default_value(HOST_IP))
    ("u,udp-port", "UDP output port (default: 5000)",
        cxxopts::value<std::string>()->default_value(""))
    ("z,zmq-port", "ZMQ publisher port (default: 7000)",
        cxxopts::value<std::string>()->default_value(""))
    ("use-multi-process", "Run media library as multi process client-service instead of unified process",
        cxxopts::value<bool>()->default_value("false"));
    // clang-format on

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

    if (result.count("print-fps"))
    {
        arguments.push_back(ArgumentType::PrintFPS);
    }

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("config-file-path"))
    {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("profile"))
    {
        arguments.push_back(ArgumentType::Profile);
    }

    if (result.count("host-ip"))
    {
        arguments.push_back(ArgumentType::HostIP);
    }

    if (result.count("udp-port") && !result["udp-port"].as<std::string>().empty())
    {
        arguments.push_back(ArgumentType::UdpPort);
    }

    if (result.count("zmq-port") && !result["zmq-port"].as<std::string>().empty())
    {
        arguments.push_back(ArgumentType::ZmqPort);
    }

    // Handle unrecognized options
    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}

struct AppResources
{
    MediaLibraryInterfacePtr media_library;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps;
    bool use_multi_process = false;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string udp_port;
    std::string zmq_port;
};

std::string read_string_from_file(const char *file_path)
{
    cloexec::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error(std::string("config path (") + file_path + ") is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

/**
 * @brief Configure the frontend and encoders for the application.
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_string = read_string_from_file(app_resources->medialib_config_path.c_str());

    if (app_resources->use_multi_process)
    {
        auto media_lib_expected = media_library_service::MediaLibraryClient::create();
        if (!media_lib_expected.has_value())
        {
            std::cout << "Failed to create media library client" << std::endl;
            throw std::runtime_error("Failed to create media library client");
        }
        app_resources->media_library = media_lib_expected.value();
    }
    else
    {
        auto media_lib_expected = MediaLibrary::create();
        if (!media_lib_expected.has_value())
        {
            std::cout << "Failed to create media library" << std::endl;
            throw std::runtime_error("Failed to create media library");
        }
        app_resources->media_library = media_lib_expected.value();
    }
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        throw std::runtime_error("Failed to initialize media library");
    }
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        app_resources->media_library->set_profile(app_resources->profile_name);
    }
}

/**
 * @brief Create and configure the application's processing pipeline.
 *
 * This function sets up the application's processing pipeline by creating various stages
 * and subscribing them to each other to form a complete pipeline. Each stage is initialized
 * with specific parameters and then added to the pipeline. The stages are also interconnected
 * by subscribing them to ensure data flows correctly between them.
 *
 * @param app_resources Shared pointer to the application's resources, which includes the pipeline object.
 */
void create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    // Get output streams from frontend
    auto output_streams = app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
    auto vision_config = hailo_analytics::analytics::vision::base_vision_config(output_streams.value(), base_port);
    vision_config.outputs.erase(AI_SINK);
    vision_config.outputs[VISION_SINK].udp_config.host = app_resources->host_ip;
    // we generate a filled in vision_config, and erase the AI_SINK, to override the vision pipeline
    // from automatically generating and connecting frontend outputs to encoders
    auto vision_pipeline_status = hailo_analytics::analytics::vision::generate_vision_pipeline(
        app_resources->media_library, VISION_PIPELINE, vision_config);
    if (!vision_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
        throw std::runtime_error("Failed to create vision pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr vision_pipeline = vision_pipeline_status.value();

    // AI Pipeline Stages
    bool use_hailort_service = app_resources->use_multi_process;

    auto tiling_cfg = face_landmarks_app::default_tiling_config();
    tiling_cfg.detection_config.ai_config.use_hailort_service = use_hailort_service;
    auto tiling_pipeline_status =
        hailo_analytics::analytics::tiling::generate_tiling_detection_pipeline(TILING_PIPELINE, tiling_cfg);
    if (!tiling_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create tiling pipeline");
        throw std::runtime_error("Failed to create tiling pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr tiling_pipeline = tiling_pipeline_status.value();

    auto landmarks_cfg = face_landmarks_app::default_landmarks_config();
    landmarks_cfg.landmarks_config.ai_config.use_hailort_service = use_hailort_service;
    auto landmarks_pipeline_status =
        hailo_analytics::analytics::face_landmarks::generate_bbox_landmarks_pipeline(LANDMARKS_PIPELINE, landmarks_cfg);
    if (!landmarks_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create landmarks pipeline");
        throw std::runtime_error("Failed to create landmarks pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr landmarks_pipeline = landmarks_pipeline_status.value();

    // Analytic Metadata Sender Pipeline
    hailo_analytics::analytics::analytic_metadata_zmq_sender::analytic_metadata_zmq_sender_config_t
        analytics_sender_config;
    analytics_sender_config.analytic_metadata_config.queue_size = 1;
    analytics_sender_config.zeromq_config.queue_size = 1;
    if (!app_resources->zmq_port.empty())
    {
        analytics_sender_config.zeromq_config.pub_address = "tcp://*:" + app_resources->zmq_port;
    }
    auto analytic_metadata_sender_pipeline_status =
        hailo_analytics::analytics::analytic_metadata_zmq_sender::generate_analytic_metadata_zmq_sender_pipeline(
            ANALYTIC_META_SENDER_PIPELINE, analytics_sender_config);
    if (!analytic_metadata_sender_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create analytic metadata sender pipeline");
        throw std::runtime_error("Failed to create analytic metadata sender pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr analytic_metadata_sender_pipeline =
        analytic_metadata_sender_pipeline_status.value();

    // Combine Pipelines
    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE)
        .add_stage(tiling_pipeline)
        .add_stage(landmarks_pipeline)
        .add_stage(analytic_metadata_sender_pipeline, hailo_analytics::pipeline::StageType::SINK);

    // Connect vision pipeline output to tiling pipeline input
    pip_builder.connect_frontend(VISION_PIPELINE, AI_SINK, TILING_PIPELINE);
    pip_builder.connect(TILING_PIPELINE, LANDMARKS_PIPELINE);
    pip_builder.connect(LANDMARKS_PIPELINE, ANALYTIC_META_SENDER_PIPELINE);

    app_resources->pipeline = pip_builder.build(APP_NAME, true);
}

/**
 * @brief Main function to initialize and run the application.
 *
 * This function sets up the application resources, registers a signal handler for SIGINT,
 * parses user arguments, configures the frontend and encoders, creates the pipeline,
 * subscribes elements, starts the pipeline, waits for a specified timeout, and then stops the pipeline.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return int Exit status of the application.
 */
std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;
int main(int argc, char *argv[])
{
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

    // register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([](int /*signal*/) {
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
        case ArgumentType::PrintFPS:
            app_resources->print_fps = true;
            break;
        case ArgumentType::Config:
            app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
            break;
        case ArgumentType::Profile:
            app_resources->profile_name = result["profile"].as<std::string>();
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::UdpPort:
            app_resources->udp_port = result["udp-port"].as<std::string>();
            break;
        case ArgumentType::ZmqPort:
            app_resources->zmq_port = result["zmq-port"].as<std::string>();
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    app_resources->use_multi_process = result["use-multi-process"].as<bool>();

    // Configure media library
    configure_media_library(app_resources);

    // Create pipeline and stages
    create_pipeline(app_resources);

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

    // Shutdown the media library client (disconnect from gRPC service)
    app_resources->media_library->shutdown();

    return 0;
}
