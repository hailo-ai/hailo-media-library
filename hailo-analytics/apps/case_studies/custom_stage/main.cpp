// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/signal_utils.hpp"

// infra includes
#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/analytics/overlay.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

// defines
#define VISION_PIPELINE "vision_pipeline"
#define DETECTION_PIPELINE "detection_pipeline"
#define OVERLAY_PIPELINE "overlay_pipeline"
#define APP_NAME "custom_stage_app"
#define CUSTOM_STAGE "custom_stage"
#define HOST_IP "10.0.0.2"
#define VISION_SINK "sink0"
#define OVERLAY_PORT "5002"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/detection_medialib_config.json"

enum class ArgumentType
{
    Help,
    PrintFPS,
    Timeout,
    Config,
    Profile,
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
        cxxopts::value<std::string>()->default_value(HOST_IP));
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
    std::shared_ptr<MediaLibrary> media_library;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
};

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
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
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();
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

class CustomStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    CustomStage(std::string name, size_t queue_size, bool leaky = false, bool trace_processing_operations = true)
        : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
    {
    }

    hailo_analytics::pipeline::AppStatus process(hailo_analytics::pipeline::BufferPtr data) override
    {
        // Process the data here
        // For example, you can access the inference results and react accordingly
        HailoROIPtr hailo_roi = data->get_roi();
        std::vector<HailoDetectionPtr> objects = hailo_common::get_hailo_detections(hailo_roi);
        if (!objects.empty())
        {
            std::cout << "Detected " << objects.size() << " objects: ";
            for (const auto &object : objects)
            {
                std::cout << object->get_label() << ", ";
            }
            std::cout << std::endl;
        }

        // Send the data to the next subscribers in the pipeline
        send_to_subscribers(data);

        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }
};

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
    hailo_analytics::analytics::vision::vision_config_t vision_config;
    // we apply a fresh vision_config_t, which by default has no outputs set, to override the vision pipeline
    // automatically connecting frontend outputs to encoders
    auto vision_pipeline_status = hailo_analytics::analytics::vision::generate_vision_pipeline(
        app_resources->media_library, VISION_PIPELINE, vision_config);
    if (!vision_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
        throw std::runtime_error("Failed to create vision pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr vision_pipeline = vision_pipeline_status.value();

    // AI Pipeline Stages
    auto detection_pipeline_status =
        hailo_analytics::analytics::detection::generate_detection_pipeline(DETECTION_PIPELINE);
    if (!detection_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create detection pipeline");
        throw std::runtime_error("Failed to create detection pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr detection_pipeline = detection_pipeline_status.value();

    // Custom stage for processing detection results
    std::shared_ptr<CustomStage> custom_stage = std::make_shared<CustomStage>(CUSTOM_STAGE, 2, false, true);

    // Analytics Output Pipeline
    auto overlay_pipeline_status = hailo_analytics::analytics::overlay::generate_overlay_pipeline(
        app_resources->media_library->m_encoders[VISION_SINK], OVERLAY_PIPELINE);
    if (!overlay_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create overlay pipeline");
        throw std::runtime_error("Failed to create overlay pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr overlay_pipeline = overlay_pipeline_status.value();

    // Combine Pipelines
    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE)
        .add_stage(detection_pipeline)
        .add_stage(custom_stage)
        .add_stage(overlay_pipeline, hailo_analytics::pipeline::StageType::SINK);

    // Connect vision pipeline output to detection pipeline input
    pip_builder.connect_frontend(VISION_PIPELINE, VISION_SINK, DETECTION_PIPELINE);
    pip_builder.connect(DETECTION_PIPELINE, CUSTOM_STAGE);
    pip_builder.connect(CUSTOM_STAGE, OVERLAY_PIPELINE);

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
        case ArgumentType::Error:
            return 1;
        }
    }

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
    return 0;
}
