// general includes
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>
#include <stdlib.h>
#include <media_library/media_library.hpp>
#include <media_library/media_library_api_types.hpp>
#include <cstdio>
#include <iostream>
#include <map>
#include <chrono>
#include <condition_variable>
#include <initializer_list>
#include <iterator>
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
#include "config_path_utils.hpp"
#include "lpr_pipeline_builder.hpp"
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/analytics/analytic_metadata_zmq_sender.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/utils/profile_utils.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage_from_file.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"

// constants
static constexpr const char *VISION_PIPELINE = "vision_pipeline";
static constexpr const char *TILING_PIPELINE = "tiling_pipeline";
static constexpr const char *OCR_PIPELINE = "ocr_pipeline";
static constexpr const char *ANALYTIC_META_SENDER_PIPELINE = "analytic_metadata_sender_pipeline";
static constexpr const char *APP_NAME = "lpr_app";
static const std::string HOST_IP = hailo_analytics::analytics::vision::get_default_host_ip();
static constexpr const char *VISION_SINK = "sink0";
static constexpr const char *AI_SINK = "sink2";
static constexpr const char *NO_PROFILE_SELECTED = "";
static constexpr const char *MEDIALIB_CONFIG_PATH =
    "/etc/imaging/cfg/medialib_configs/face_landmarks_medialib_config.json";

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
    FileSource,
    TrackingMode,
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
    ("f,file", "Path to raw NV12 video file (enables file source mode)",
        cxxopts::value<std::string>()->default_value(""))
    ("W,width", "Video width (required when --file is used)",
        cxxopts::value<int>()->default_value("0"))
    ("H,height", "Video height (required when --file is used)",
        cxxopts::value<int>()->default_value("0"))
    ("fps", "Playback FPS (used with --file)",
        cxxopts::value<double>()->default_value("30"))
    ("m,tracking-mode", "Tracking mode for LPR: slow, fast, balanced (default: balanced)",
        cxxopts::value<std::string>()->default_value("balanced"));
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

    if (result.count("file") && !result["file"].as<std::string>().empty())
    {
        arguments.push_back(ArgumentType::FileSource);
    }

    if (result.count("tracking-mode"))
    {
        arguments.push_back(ArgumentType::TrackingMode);
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
    std::string file_path;
    int width = 0;
    int height = 0;
    double fps = 30.0;
    lpr_app::TrackingMode tracking_mode = lpr_app::TrackingMode::BALANCED;
    std::vector<std::string> temp_config_files;
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

nlohmann::json read_json_file(const std::string &path)
{
    std::string content = read_string_from_file(path.c_str());
    return nlohmann::json::parse(content);
}

std::string write_temp_json(const std::string &name, const nlohmann::json &content,
                            std::vector<std::string> &temp_files)
{
    std::string path = "/tmp/lpr_" + name + ".json";
    cloexec::ofstream out(path);
    if (!out.is_open())
    {
        throw std::runtime_error("Failed to write temp config file: " + path);
    }
    out << content.dump(4);
    out.close();
    temp_files.push_back(path);
    return path;
}

std::string prepare_config_for_file_mode(const std::string &config_string, int width, int height, double fps,
                                         std::vector<std::string> &temp_files)
{
    nlohmann::json main_config = nlohmann::json::parse(config_string);

    // Section overrides to apply to sub-config files
    std::map<std::string, nlohmann::json> section_overrides;
    section_overrides["sensor_config"] = {
        {"input_video",
         {{"source_type", "APPSRC"},
          {"resolution", {{"width", width}, {"height", height}, {"framerate", static_cast<int>(fps)}}}}}};
    section_overrides["iq_settings"] = {{"dewarp", {{"enabled", false}}}};
    section_overrides["stabilizer_settings"] = {
        {"dis", {{"enabled", false}}}, {"eis", {{"enabled", false}}}, {"gyro", {{"enabled", false}}}};
    section_overrides["application_settings"] = {{"optical_zoom", {{"enabled", false}}}};

    for (auto &profile : main_config["profiles"])
    {
        std::string profile_name = profile["name"].get<std::string>();
        std::string config_file_path = profile["config_file"].get<std::string>();
        nlohmann::json profile_config = read_json_file(config_file_path);

        for (const auto &[section_key, overrides] : section_overrides)
        {
            if (!profile_config.contains(section_key) || !profile_config[section_key].is_string())
            {
                continue;
            }
            std::string sub_config_path = profile_config[section_key].get<std::string>();
            nlohmann::json sub_config = read_json_file(sub_config_path);
            sub_config.merge_patch(overrides);

            std::string temp_name = profile_name + "_" + section_key;
            std::string temp_path = write_temp_json(temp_name, sub_config, temp_files);
            profile_config[section_key] = temp_path;
        }

        std::string temp_profile_path = write_temp_json(profile_name + "_profile", profile_config, temp_files);
        profile["config_file"] = temp_profile_path;
    }

    // Skip metadata validation since we modified config files
    setenv("HAILO_MEDIA_LIB_SKIP_METADATA_CONFIG_VALIDATION", "1", 1);

    return main_config.dump();
}

void configure_media_library(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_string = apps::utils::resolve_relative_refs(
        read_string_from_file(app_resources->medialib_config_path.c_str()), app_resources->medialib_config_path);

    if (!app_resources->file_path.empty())
    {
        medialib_config_string =
            prepare_config_for_file_mode(medialib_config_string, app_resources->width, app_resources->height,
                                         app_resources->fps, app_resources->temp_config_files);
    }

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
        hailo_analytics::utils::set_initial_profile(app_resources->media_library, app_resources->profile_name);
    }
}

hailo_analytics::pipeline::PipelinePtr create_vision_pipeline_from_file(std::shared_ptr<AppResources> app_resources)
{
    namespace sources = hailo_analytics::pipeline::sources;
    namespace vision = hailo_analytics::analytics::vision;

    // Create frontend from file (APPSRC already configured via pre-init config)
    auto frontend_from_file = sources::FrontendStageFromFileBuild::create()
                                  .set_stage_name("frontend_stage")
                                  .set_file_location(app_resources->file_path)
                                  .set_width(app_resources->width)
                                  .set_height(app_resources->height)
                                  .set_fps(app_resources->fps)
                                  .set_loop_enabled_opt(true)
                                  .set_buffer_pool_size(15)
                                  .buildptr();

    hailo_analytics::pipeline::AppStatus frontend_config_status =
        frontend_from_file->configure(app_resources->media_library);
    if (frontend_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to configure frontend from file");
        throw std::runtime_error("Failed to configure frontend from file");
    }

    // Get output streams and build vision config
    auto output_streams = app_resources->media_library->m_frontend->get_outputs_streams();
    if (!output_streams.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
        throw std::runtime_error("Failed to get stream ids");
    }

    int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
    auto vision_config = vision::base_vision_config(output_streams.value(), base_port);
    vision_config.outputs.erase(AI_SINK);
    vision_config.outputs[VISION_SINK].udp_config.host = app_resources->host_ip;

    // Build the vision pipeline manually with our file frontend
    hailo_analytics::pipeline::PipelineBuilder pip_builder;
    pip_builder.add_stage(frontend_from_file, hailo_analytics::pipeline::StageType::SOURCE);

    for (const auto &[stream_id, output_config] : vision_config.outputs)
    {
        std::string output_pipeline_name = std::string(VISION_PIPELINE) + "_output_" + stream_id;
        auto output_result = vision::generate_vision_output_pipeline(app_resources->media_library, stream_id,
                                                                     output_pipeline_name, output_config);
        if (!output_result.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create vision output pipeline for {}", stream_id);
            throw std::runtime_error("Failed to create vision output pipeline");
        }
        pip_builder.add_stage(output_result.value(), hailo_analytics::pipeline::StageType::SINK);
        pip_builder.connect_frontend(*vision_config.frontend_config.stage_name, stream_id, output_pipeline_name);
    }

    hailo_analytics::pipeline::PipelinePtr pipeline = pip_builder.build(VISION_PIPELINE, true);
    pipeline->set_in_stage(frontend_from_file);
    pipeline->set_out_stage(frontend_from_file);

    return pipeline;
}

void create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    hailo_analytics::pipeline::PipelinePtr vision_pipeline;

    if (!app_resources->file_path.empty())
    {
        // File source mode
        vision_pipeline = create_vision_pipeline_from_file(app_resources);
    }
    else
    {
        // Camera mode (original behavior)
        auto output_streams = app_resources->media_library->m_frontend->get_outputs_streams();
        if (!output_streams.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get stream ids");
            throw std::runtime_error("Failed to get stream ids");
        }

        int base_port = app_resources->udp_port.empty() ? 5000 : std::stoi(app_resources->udp_port);
        auto vision_config = hailo_analytics::analytics::vision::base_vision_config(output_streams.value(), base_port);
        vision_config.outputs.erase(AI_SINK);
        vision_config.outputs[VISION_SINK].udp_config.host = app_resources->host_ip;
        auto vision_pipeline_status = hailo_analytics::analytics::vision::generate_vision_pipeline(
            app_resources->media_library, VISION_PIPELINE, vision_config);
        if (!vision_pipeline_status.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create vision pipeline");
            throw std::runtime_error("Failed to create vision pipeline");
        }
        vision_pipeline = vision_pipeline_status.value();
    }

    // AI Pipeline Stages
    auto tiling_pipeline_status = lpr_app::build_tiling_pipeline(TILING_PIPELINE, app_resources->tracking_mode);
    if (!tiling_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create tiling pipeline");
        throw std::runtime_error("Failed to create tiling pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr tiling_pipeline = tiling_pipeline_status.value();

    auto ocr_pipeline_status = lpr_app::build_ocr_pipeline(OCR_PIPELINE);
    if (!ocr_pipeline_status.has_value())
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create OCR pipeline");
        throw std::runtime_error("Failed to create OCR pipeline");
    }
    hailo_analytics::pipeline::PipelinePtr ocr_pipeline = ocr_pipeline_status.value();

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
        .add_stage(ocr_pipeline)
        .add_stage(analytic_metadata_sender_pipeline, hailo_analytics::pipeline::StageType::SINK);

    // Connect vision pipeline output to tiling pipeline input
    pip_builder.connect_frontend(VISION_PIPELINE, AI_SINK, TILING_PIPELINE);
    pip_builder.connect(TILING_PIPELINE, OCR_PIPELINE);
    pip_builder.connect(OCR_PIPELINE, ANALYTIC_META_SENDER_PIPELINE);

    app_resources->pipeline = pip_builder.build(APP_NAME, true);
}

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
        case ArgumentType::FileSource:
            app_resources->file_path = result["file"].as<std::string>();
            app_resources->width = result["width"].as<int>();
            app_resources->height = result["height"].as<int>();
            app_resources->fps = result["fps"].as<double>();
            if (app_resources->width <= 0 || app_resources->height <= 0)
            {
                std::cerr << "Error: --width and --height are required when --file is used" << std::endl;
                return 1;
            }
            break;
        case ArgumentType::TrackingMode:
            try
            {
                app_resources->tracking_mode =
                    lpr_app::tracking_mode_from_string(result["tracking-mode"].as<std::string>());
            }
            catch (const std::invalid_argument &err)
            {
                std::cerr << "Error: " << err.what() << std::endl;
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

    for (const auto &temp_file : app_resources->temp_config_files)
    {
        std::remove(temp_file.c_str());
    }

    return 0;
}
