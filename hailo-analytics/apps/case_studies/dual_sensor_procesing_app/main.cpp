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
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "media_library/signal_utils.hpp"
// infra includes
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/utils/profile_utils.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

using hailo_analytics::pipeline::AppStatus;

// defines
#define APP_NAME "dual_sensor_app"
#define HOST_IP hailo_analytics::analytics::vision::get_default_host_ip()
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH_SENSOR_0                                                                                  \
    "/etc/imaging/cfg/medialib_configs/case_studies/dual_sensor_single_stream_medialib_config_sensor_0.json"
#define MEDIALIB_CONFIG_PATH_SENSOR_1                                                                                  \
    "/etc/imaging/cfg/medialib_configs/case_studies/dual_sensor_single_stream_medialib_config_sensor_1.json"

#undef PORT_FROM_ID
// Use rfind to handle prefixed stream IDs like "dpm_sink1"
#define PORT_FROM_ID(sensor_idx, sink_id)                                                                              \
    std::to_string(5000 + (sensor_idx) * 100 + std::stoi((sink_id).substr((sink_id).rfind("sink") + 4)) * 2)

enum class ArgumentType
{
    Help,
    PrintFPS,
    Timeout,
    ConfigSensor0,
    ConfigSensor1,
    ProfileSensor0,
    ProfileSensor1,
    HostIP,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("Dual sensor processing app case study");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run", 
        cxxopts::value<int>()->default_value("60"))
    ("p,print-fps", "Print FPS", 
        cxxopts::value<bool>()->default_value("false"))
    ("c,config-file-path-sensor-0", "Media library configuration path for sensor 0",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH_SENSOR_0))
    ("d,config-file-path-sensor-1", "Media library configuration path for sensor 1",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH_SENSOR_1))
    ("a,profile-sensor-0", "Profile name for sensor 0", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("b,profile-sensor-1", "Profile name for sensor 1", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("o,host-ip", "Host IP address for UDP output", 
        cxxopts::value<std::string>()->default_value(HOST_IP));
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

    // Handle unrecognized options
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
    bool print_fps;
    bool use_medialib_service = false;
    std::string host_ip = HOST_IP;

    AppResources()
    {
        // Initialize instances vector with appropriate size
        instances.resize(NUM_SENSORS);
    }
};

/**
 * @brief Configure the frontend and encoders for the application.
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
AppStatus configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    // One service owns one media library, and its two buffer-transport sockets live at fixed
    // paths, so a second instance cannot be started alongside it. Until the service supports
    // that, a two-sensor app has to host both libraries itself.
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
    // Application Pipeline Builder
    hailo_analytics::pipeline::PipelineBuilder pip_builder;

    for (int i = 0; i < AppResources::NUM_SENSORS; ++i)
    {
        // Get output streams from frontend to configure each output with unique port
        auto output_streams = app_resources->instances[i].media_library->get_frontend_output_streams();
        if (!output_streams.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get output streams for sensor {}", i);
            return AppStatus::MEDIA_LIBRARY_ERROR;
        }

        hailo_analytics::analytics::vision::vision_config_t custom_config =
            hailo_analytics::analytics::vision::base_vision_config(output_streams.value());

        // Override port for each output stream based on sensor index
        for (auto &[stream_id, output_config] : custom_config.outputs)
        {
            std::string port = PORT_FROM_ID(i, stream_id);
            output_config.udp_config.port = port;
            std::cout << "UDP Port: Sensor " << i << ", Stream '" << stream_id << "' -> " << app_resources->host_ip
                      << ":" << port << std::endl;
        }

        auto sensor_pipeline = hailo_analytics::analytics::vision::generate_vision_pipeline(
            app_resources->instances[i].media_library, "sensor_" + std::to_string(i) + "_pipeline", custom_config);
        if (!sensor_pipeline.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline for sensor {}", i);
            return sensor_pipeline.error();
        }
        pip_builder.add_stage(sensor_pipeline.value());
    }

    app_resources->pipeline = pip_builder.build(APP_NAME, true);

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
    app_resources->instances[0].medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR_0;
    app_resources->instances[1].medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR_1;

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
        case ArgumentType::Error:
            return 1;
        }
    }

    app_resources->use_medialib_service = result[apps::utils::USE_MEDIALIB_SERVICE_OPTION].as<bool>();

    // Configure the media library and build the pipeline. Both read configuration the media
    // library service may have built for a different application, so a mismatch surfaces here
    // as an exception; report it and exit rather than letting it reach std::terminate.
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
