// general includes
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/signal_utils.hpp"

// infra includes
#include "hailo_analytics/analytics/dpm_analytics.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

// defines
#define VISION_PIPELINE "vision_pipeline"
#define DPM_AI_PIPELINE "dpm_ai_pipeline"
#define APP_NAME "dynamic_privacy_mask_app"
#define HOST_IP "10.0.0.2"
#define VISION_SINK "sink0"
#define AI_SINK "dpm_sink2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/dynamic_privacy_mask_medialib_config.json"

// Import shared DPM constants
using hailo_analytics::analytics::dpm_analytics::DEFAULT_DPM_SEG_HEF;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15H;
using hailo_analytics::analytics::dpm_analytics::DEFAULT_MAX_DETECTIONS_15L;

#define SEGMENTED_LABELS_DEFAULT "person,vehicle"

bool is_hailo15l()
{
    std::ifstream file("/sys/devices/soc0/machine");
    if (!file.is_open())
        return false;
    std::string line;
    std::getline(file, line);
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);
    return line.find("hailo-15l") != std::string::npos;
}

int default_max_detections()
{
    return is_hailo15l() ? DEFAULT_MAX_DETECTIONS_15L : DEFAULT_MAX_DETECTIONS_15H;
}

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
    HefPath,
    SegmentLabels,
    MaxDetections,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("Dynamic Privacy Mask app");
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
    ("e,hef-path", "Segmentation HEF file path", 
        cxxopts::value<std::string>()->default_value(std::string(DEFAULT_DPM_SEG_HEF)))
    ("s,segment-labels", "Comma-separated list of labels to segment (e.g., 'person,face,vehicle')", 
        cxxopts::value<std::string>()->default_value(SEGMENTED_LABELS_DEFAULT))
    ("n,max-detections", "Maximum number of detections to process per frame",
        cxxopts::value<int>()->default_value(std::to_string(default_max_detections())));
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

    if (result.count("hef-path"))
    {
        arguments.push_back(ArgumentType::HefPath);
    }

    if (result.count("segment-labels"))
    {
        arguments.push_back(ArgumentType::SegmentLabels);
    }

    if (result.count("max-detections"))
    {
        arguments.push_back(ArgumentType::MaxDetections);
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
    std::string udp_port;
    std::string zmq_port;
    std::string hef_path = std::string(DEFAULT_DPM_SEG_HEF);
    std::vector<std::string> segment_labels;
    int max_detections_per_frame = default_max_detections();
};

std::vector<std::string> parse_segment_labels(const std::string &labels_str)
{
    std::vector<std::string> labels;
    if (labels_str.empty())
        return labels;

    std::stringstream ss(labels_str);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (!token.empty())
        {
            labels.push_back(token);
        }
    }
    return labels;
}

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
 * @brief Configure the media library for the application.
 *
 * This function initializes the media library with configuration from file.
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

struct stream_resolutions_t
{
    int vision_width = 0;
    int vision_height = 0;
    int ai_width = 0;
    int ai_height = 0;
};

stream_resolutions_t get_stream_resolutions(const std::vector<frontend_output_stream_t> &output_streams)
{
    stream_resolutions_t resolutions;
    for (const auto &stream : output_streams)
    {
        if (stream.id == VISION_SINK)
        {
            resolutions.vision_width = stream.width;
            resolutions.vision_height = stream.height;
        }
        else if (stream.id == AI_SINK)
        {
            resolutions.ai_width = stream.width;
            resolutions.ai_height = stream.height;
        }
    }

    if (resolutions.vision_width == 0 || resolutions.vision_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get vision input resolution from frontend");
        throw std::runtime_error("Failed to get vision input resolution from frontend");
    }

    if (resolutions.ai_width == 0 || resolutions.ai_height == 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get AI pipeline resolution from frontend");
        throw std::runtime_error("Failed to get AI pipeline resolution from frontend");
    }

    return resolutions;
}

hailo_analytics::pipeline::PipelinePtr create_vision_pipeline(
    std::shared_ptr<AppResources> app_resources, const std::vector<frontend_output_stream_t> &output_streams)
{
    int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
    auto vision_config = hailo_analytics::analytics::vision::base_vision_config(output_streams, base_port);
    // Erase the AI sink to prevent vision pipeline from automatically generating
    // and connecting frontend outputs to encoders for it
    vision_config.outputs.erase(AI_SINK);
    vision_config.outputs[VISION_SINK].udp_config.host = app_resources->host_ip;

    auto vision_pipeline_status = hailo_analytics::analytics::vision::generate_vision_pipeline(
        *app_resources->media_library, VISION_PIPELINE, vision_config);
    if (!vision_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
        throw std::runtime_error("Failed to create vision pipeline");
    }
    return vision_pipeline_status.value();
}

hailo_analytics::pipeline::PipelinePtr create_dpm_ai_pipeline(std::shared_ptr<AppResources> app_resources,
                                                              const stream_resolutions_t &resolutions)
{
    auto dpm_ai_config = hailo_analytics::analytics::dpm_analytics::build_dpm_config(
        resolutions.ai_width, resolutions.ai_height, app_resources->max_detections_per_frame,
        app_resources->segment_labels, app_resources->hef_path);

    if (!app_resources->zmq_port.empty() && dpm_ai_config.metadata_sender_config.has_value())
    {
        dpm_ai_config.metadata_sender_config->zeromq_config.pub_address = "tcp://*:" + app_resources->zmq_port;
    }

    auto dpm_ai_pipeline_status =
        hailo_analytics::analytics::dpm_analytics::generate_full_dpm_analytics_pipeline(DPM_AI_PIPELINE, dpm_ai_config);
    if (!dpm_ai_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create DPM analytics pipeline");
        throw std::runtime_error("Failed to create DPM analytics pipeline");
    }
    return dpm_ai_pipeline_status.value();
}

/**
 * @brief Create and configure the application's processing pipeline.
 *
 * This function sets up the application's processing pipeline using the analytics generators:
 * - Vision pipeline for output encoding and streaming
 * - DPM AI pipeline for tiling detection + segmentation + privacy masking
 *
 * @param app_resources Shared pointer to the application's resources, which includes the pipeline object.
 */
void create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    auto output_streams = app_resources->media_library->m_frontend->get_outputs_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    auto resolutions = get_stream_resolutions(output_streams.value());
    auto vision_pipeline = create_vision_pipeline(app_resources, output_streams.value());
    auto dpm_ai_pipeline = create_dpm_ai_pipeline(app_resources, resolutions);

    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE);
    pip_builder.add_stage(dpm_ai_pipeline);

    // Connect frontend AI sink to the DPM analytics pipeline
    pip_builder.connect_frontend(VISION_PIPELINE, AI_SINK, DPM_AI_PIPELINE);

    app_resources->pipeline = pip_builder.build(APP_NAME, true);
}

/**
 * @brief Main function to initialize and run the application.
 *
 * This function sets up the application resources, registers a signal handler for SIGINT,
 * parses user arguments, configures the media library, creates the pipeline,
 * starts the pipeline, waits for a specified timeout, and then stops the pipeline.
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
    app_resources->segment_labels = parse_segment_labels(SEGMENTED_LABELS_DEFAULT);

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
        case ArgumentType::UdpPort:
            app_resources->udp_port = result["udp-port"].as<std::string>();
            break;
        case ArgumentType::ZmqPort:
            app_resources->zmq_port = result["zmq-port"].as<std::string>();
            break;
        case ArgumentType::HefPath:
            app_resources->hef_path = result["hef-path"].as<std::string>();
            break;
        case ArgumentType::SegmentLabels:
            app_resources->segment_labels = parse_segment_labels(result["segment-labels"].as<std::string>());
            break;
        case ArgumentType::MaxDetections:
            app_resources->max_detections_per_frame = result["max-detections"].as<int>();
            if (app_resources->max_detections_per_frame < 1)
            {
                std::cerr << "Error: --max-detections must be at least 1" << std::endl;
                return 1;
            }
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
