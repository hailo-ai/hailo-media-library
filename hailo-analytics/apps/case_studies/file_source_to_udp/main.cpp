/**
 * @file main.cpp
 * @brief File Source to UDP Case Study - Minimal pipeline: FileSourceStage -> EncoderStage -> UdpStage
 */

// general includes
#include <iostream>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <string>
#include <stdexcept>
#include <iterator>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/signal_utils.hpp"
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"

// infra includes
#include "hailo_analytics/analytics/vision.hpp"
#include "hailo_analytics/pipeline/sources/file_source_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

// define
#define APP_NAME "file_source_app"

std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;

struct PipelineConfig
{
    std::string file;
    int width;
    int height;
    int fps;
    std::string host;
    std::string port;
    int timeout;
    std::string encoder_config_path;
    bool print_fps;
};

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error(std::string("config path (") + file_path + ") is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    return file_string;
}

PipelineConfig parse_arguments(int argc, char *argv[])
{
    cxxopts::Options options("File Source to UDP Case Study", "Pipeline: FileSourceStage -> EncoderStage -> UdpStage");
    options.add_options()("h,help", "Show help")("f,file", "Video file path (NV12)", cxxopts::value<std::string>())(
        "w,width", "Video width", cxxopts::value<int>())("g,height", "Video height", cxxopts::value<int>())(
        "r,fps", "Frame rate", cxxopts::value<int>()->default_value("30"))(
        "o,host", "Target IP", cxxopts::value<std::string>()->default_value("10.0.0.2"))(
        "p,port", "UDP port", cxxopts::value<std::string>()->default_value("5000"))(
        "t,timeout", "Timeout (seconds)", cxxopts::value<int>()->default_value("60"))(
        "e,encoder-config", "Encoder config file path",
        cxxopts::value<std::string>())("s,print-fps", "Print FPS", cxxopts::value<bool>()->default_value("false"));

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        std::cout << "\nRequired parameters:\n";
        std::cout << "  -f, --file        Video file path (NV12 format)\n";
        std::cout << "  -w, --width       Video width in pixels\n";
        std::cout << "  -g, --height      Video height in pixels\n";
        std::cout << "  -e, --encoder-config  Encoder configuration file path\n";
        std::cout << "\nExamples:\n";
        std::cout << "  " << argv[0] << " -f /path/to/video.nv12 -w 1920 -g 1080 -e /path/to/encoder.json\n";
        std::cout << "  " << argv[0] << " -f video.nv12 -w 1280 -g 720 -o 192.168.1.100 -p 6000\n";
        std::cout << "\nView stream: vlc udp://<host>:<port>\n";
        exit(0);
    }

    // Check for required parameters
    if (!result.count("file"))
    {
        std::cerr << "Error: Video file path is required (-f/--file)" << std::endl;
        exit(1);
    }
    if (!result.count("width"))
    {
        std::cerr << "Error: Video width is required (-w/--width)" << std::endl;
        exit(1);
    }
    if (!result.count("height"))
    {
        std::cerr << "Error: Video height is required (-g/--height)" << std::endl;
        exit(1);
    }
    if (!result.count("encoder-config"))
    {
        std::cerr << "Error: Encoder config file path is required (-e/--encoder-config)" << std::endl;
        exit(1);
    }

    PipelineConfig config;
    config.file = result["file"].as<std::string>();
    config.width = result["width"].as<int>();
    config.height = result["height"].as<int>();
    config.fps = result["fps"].as<int>();
    config.host = result["host"].as<std::string>();
    config.port = result["port"].as<std::string>();
    config.timeout = result["timeout"].as<int>();
    config.encoder_config_path = result["encoder-config"].as<std::string>();
    config.print_fps = result["print-fps"].as<bool>();

    // Validate file exists
    std::ifstream check_file(config.file);
    if (!check_file.good())
    {
        std::cerr << "Error: File not found: " << config.file << std::endl;
        exit(1);
    }

    return config;
}

std::shared_ptr<MediaLibraryEncoder> create_media_library_encoder(const std::string &encoder_config_path)
{
    std::string encoder_config_string = read_string_from_file(encoder_config_path.c_str());

    auto encoder_expected = MediaLibraryEncoder::create("encoder");
    if (!encoder_expected.has_value())
    {
        std::cerr << "Failed to create MediaLibraryEncoder" << std::endl;
        exit(1);
    }

    auto media_encoder = encoder_expected.value();
    if (media_encoder->set_config(encoder_config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to configure MediaLibraryEncoder" << std::endl;
        exit(1);
    }

    return media_encoder;
}

hailo_analytics::pipeline::PipelinePtr create_pipeline(const PipelineConfig &config,
                                                       std::shared_ptr<MediaLibraryEncoder> media_encoder)
{
    auto file_source = hailo_analytics::pipeline::sources::FileSourceStageBuild::create()
                           .set_stage_name("file_source")
                           .set_file_location(config.file)
                           .set_width(config.width)
                           .set_height(config.height)
                           .set_fps(config.fps)
                           .set_buffer_pool_size(10)
                           .buildptr();

    // Generate output pipeline (encoder -> UDP)
    hailo_analytics::analytics::vision::vision_output_config_t output_config;
    output_config.udp_config.host = config.host;
    output_config.udp_config.port = config.port;
    auto output_result = hailo_analytics::analytics::vision::generate_vision_output_pipeline(
        media_encoder, "file_output", output_config);
    if (!output_result.has_value())
    {
        std::cerr << "Failed to create encoder output pipeline" << std::endl;
        exit(1);
    }
    hailo_analytics::pipeline::PipelinePtr output_pipeline = output_result.value();

    hailo_analytics::pipeline::PipelineBuilder builder;
    builder.add_stage(file_source, hailo_analytics::pipeline::StageType::SOURCE)
        .add_stage(output_pipeline, hailo_analytics::pipeline::StageType::SINK)
        .connect("file_source", "file_output");

    return builder.build(APP_NAME, true);
}

void run_pipeline(const PipelineConfig &config, hailo_analytics::pipeline::PipelinePtr pipeline)
{
    std::cout << "Streaming " << config.file << " (" << config.width << "x" << config.height << "@" << config.fps
              << "fps) to udp://" << config.host << ":" << config.port << std::endl;

    pipeline->start();

    std::unique_lock<std::mutex> lock(g_stop_mutex);
    g_stop_cv.wait_for(lock, std::chrono::seconds(config.timeout));

    pipeline->stop();
    std::cout << "Done." << std::endl;
}

int main(int argc, char *argv[])
{
    try
    {
        // Parse arguments
        PipelineConfig config = parse_arguments(argc, argv);

        // Setup signal handler
        signal_utils::SignalHandler signal_handler(false);
        signal_handler.register_signal_handler([](int) { g_stop_cv.notify_all(); });

        // Create MediaLibrary encoder
        auto media_encoder = create_media_library_encoder(config.encoder_config_path);

        // Create pipeline
        auto pipeline = create_pipeline(config, media_encoder);

        // Run pipeline
        run_pipeline(config, pipeline);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
