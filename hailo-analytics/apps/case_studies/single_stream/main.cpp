// general includes
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
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
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/utils/profile_utils.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

using hailo_analytics::pipeline::AppStatus;

// defines
#define HOST_IP hailo_analytics::analytics::vision::get_default_host_ip()
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/single_stream_medialib_config.json"

enum class ArgumentType
{
    Help,
    PrintFPS,
    Timeout,
    Config,
    Profile,
    HostIP,
    UdpPort,
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
    bool use_medialib_service = false;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string udp_port;
};

/**
 * @brief Create the media library this app drives and apply its configuration.
 *
 * Connects to media_library_service unless the app was asked to host the library itself, then
 * initializes it and selects the requested profile.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
AppStatus configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    auto media_library =
        apps::utils::create_media_library(app_resources->use_medialib_service, app_resources->medialib_config_path);
    if (!media_library.has_value())
    {
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }
    app_resources->media_library = media_library.value();
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        hailo_analytics::utils::set_initial_profile(app_resources->media_library, app_resources->profile_name);
    }
    return AppStatus::SUCCESS;
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
AppStatus create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    // Get output streams from frontend to configure all outputs
    auto output_streams = app_resources->media_library->get_frontend_output_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get output streams from frontend");
        return AppStatus::MEDIA_LIBRARY_ERROR;
    }

    // Create base vision configuration and override host IP
    int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
    auto vision_config = hailo_analytics::analytics::vision::base_vision_config(output_streams.value(), base_port);
    // Override host IP for all output streams (single stream: sink0)
    for (auto &[id, output] : vision_config.outputs)
    {
        output.udp_config.host = app_resources->host_ip;
    }

    // Vision Pipeline with user configuration
    auto pipeline = hailo_analytics::analytics::vision::generate_vision_pipeline(
        app_resources->media_library, "single_stream_pipeline", vision_config);
    if (!pipeline.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
        return pipeline.error();
    }
    app_resources->pipeline = pipeline.value();

    return AppStatus::SUCCESS;
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
        case ArgumentType::Error:
            return 1;
        }
    }

    app_resources->use_medialib_service = result[apps::utils::USE_MEDIALIB_SERVICE_OPTION].as<bool>();

    // Configure the media library and build the pipeline. Both read configuration the media
    // library service may have built for a different application. configure_media_library()
    // reports that as a status; pipeline construction still reports it by throwing.
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

    // Shutdown the media library client (disconnect from gRPC service)
    app_resources->media_library->shutdown();

    return 0;
}
