// general includes
#include <set>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>
#include <condition_variable>
#include <mutex>

// medialibrary includes
#include "media_library/media_library.hpp"
#include "media_library/signal_utils.hpp"

// hailo analytics includes
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"

// app includes
#include "../constants.hpp"
#include "../ai_pipeline_builder.hpp"

// Frontend Params
#define FRONTEND_STAGE_SENSOR0 "frontend_stage_sensor0"
#define FRONTEND_STAGE_SENSOR1 "frontend_stage_sensor1"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH_SENSOR0                                                                                   \
    "/etc/imaging/cfg/medialib_configs/ai_example_dual_sensor_medialib_config_sensor_0.json"
#define MEDIALIB_CONFIG_PATH_SENSOR1                                                                                   \
    "/etc/imaging/cfg/medialib_configs/ai_example_dual_sensor_medialib_config_sensor_1.json"

// Macro that calculates port from sensor_idx and sink_id
#define PORT_FROM_ID(sensor_idx, sink_id) std::to_string(5000 + (sensor_idx) * 100 + std::stoi((sink_id).substr(4)) * 2)

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    ConfigSensor0,
    ConfigSensor1,
    ProfileSensor0,
    ProfileSensor1,
    DrawOverlay,
    FullLandmarks,
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
    ("l,print-latency", "Print Latency", 
        cxxopts::value<bool>()->default_value("false"))
    ("c,config-file-path-sensor-0", "Media library configuration path for sensor 0, used for AI pipeline",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH_SENSOR0))
    ("d,config-file-path-sensor-1", "Media library configuration path for sensor 1, used for simple stream",
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH_SENSOR1))
    ("a,profile-sensor-0", "Profile name for sensor 0, used for AI pipeline", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("b,profile-sensor-1", "Profile name for sensor 1, used for simple stream", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("e,draw-overlay", "Comma-separated list of stream numbers to draw overlay on (e.g., '0,1,2...'), will ignore any non-existing streams, only used for sensor 0 AI pipeline", 
        cxxopts::value<std::string>()->default_value("1"))
    ("f,full-landmarks", "Draw all landmarks (default draws only eyes for face landmarks), only used for sensor 0 AI pipeline", 
        cxxopts::value<bool>()->default_value("false"))
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

    if (result.count("print-latency"))
    {
        arguments.push_back(ArgumentType::PrintLatency);
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

    if (result.count("draw-overlay"))
    {
        arguments.push_back(ArgumentType::DrawOverlay);
    }

    if (result.count("full-landmarks"))
    {
        arguments.push_back(ArgumentType::FullLandmarks);
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
/**
 * @brief Holds all resources for a single media library instance, each instance works with one sensor.
 *
 * This structure contains all the components and modules that vary from media library instance to instance,
 * including media library, frontend, encoders, UDP outputs, and config path.
 */
struct InstanceResources
{
    std::shared_ptr<MediaLibrary> media_library;
    std::shared_ptr<hailo_analytics::pipeline::sources::FrontendStage> frontend;
    std::map<output_stream_id_t, std::shared_ptr<hailo_analytics::pipeline::codecs::EncoderStage>> encoders;
    std::map<output_stream_id_t, std::shared_ptr<hailo_analytics::pipeline::sinks::UdpStage>> udp_outputs;
    std::string medialib_config_path;
    std::string profile_name;

    void clear()
    {
        frontend = nullptr;
        encoders.clear();
        udp_outputs.clear();
        medialib_config_path = "";
        profile_name = NO_PROFILE_SELECTED;
        media_library = nullptr;
    }
};

/**
 * @brief Holds the resources required for the application.
 *
 * This structure contains pointers to various components and modules
 * used by the application, including the frontend, encoders, UDP outputs,
 * and the pipeline. It also includes a flag for controlling whether FPS (frames per second) information should be
 * printed.
 */
struct AppResources
{
    // Sensor0 resources (AI pipeline)
    InstanceResources sensor0;

    // Sensor1 resources (simple stream)
    InstanceResources sensor1;

    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps;
    bool print_latency;
    std::set<uint> draw_overlay_streams = {1};
    bool full_landmarks;
    std::string host_ip = HOST_IP;

    void clear()
    {
        pipeline = nullptr;
        sensor0.clear();
        sensor1.clear();
        print_fps = false;
        print_latency = false;
        draw_overlay_streams.clear();
        full_landmarks = false;
        host_ip = HOST_IP;
    }

    ~AppResources()
    {
        clear();
    }
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
 * @brief Create and configure an encoder and its corresponding UDP output for any sensor.
 *
 * This function sets up an encoder and a UDP output module for a given stream ID and sensor.
 * It reads configuration files and initializes the encoder and UDP module accordingly.
 *
 * @param sensor_idx The sensor index (0 or 1).
 * @param id The ID of the output stream.
 * @param instance_resources Reference to the sensor's instance resources.
 * @param host_ip The host IP address for UDP output.
 */
void create_encoder_and_udp(int sensor_idx, const std::string &id, InstanceResources &instance_resources,
                            const std::string &host_ip)
{
    // Create and configure encoder
    std::string enc_name = "enc_sensor" + std::to_string(sensor_idx) + "_" + id;
    std::cout << "Creating encoder " << enc_name << std::endl;
    std::shared_ptr<hailo_analytics::pipeline::codecs::EncoderStage> encoder_stage =
        hailo_analytics::pipeline::codecs::EncoderStageBuild::create()
            .set_stage_name(enc_name)
            .set_trace_opt(true)
            .buildptr();

    instance_resources.encoders[id] = encoder_stage;
    hailo_analytics::pipeline::AppStatus enc_config_status =
        encoder_stage->configure(instance_resources.media_library->m_encoders[id]);
    if (enc_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder " << enc_name << std::endl;
        throw std::runtime_error("Failed to configure encoder");
    }

    // Create and configure udp
    std::string udp_name = "udp_sensor" + std::to_string(sensor_idx) + "_" + id;
    std::cout << "Creating udp " << udp_name << std::endl;
    std::shared_ptr<hailo_analytics::pipeline::sinks::UdpStage> udp_stage =
        hailo_analytics::pipeline::sinks::UdpStageBuild::create()
            .set_stage_name(udp_name)
            .set_leaky_opt(false)
            .set_trace_opt(true)
            .set_printfps_opt(true)
            .buildptr();
    instance_resources.udp_outputs[id] = udp_stage;
    std::string port = PORT_FROM_ID(sensor_idx, id);
    hailo_analytics::pipeline::AppStatus udp_config_status =
        udp_stage->configure(host_ip, port, hailo_analytics::pipeline::sinks::EncodingType::H264);
    if (udp_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }
    std::cout << "UDP Port: Sensor" << sensor_idx << ", Stream '" << id << "' -> " << host_ip << ":" << port
              << std::endl;
}

/**
 * @brief Configure a sensor (generic version for both sensor0 and sensor1).
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param sensor_idx The sensor index (0 or 1).
 * @param instance_resources Reference to the sensor's instance resources.
 * @param frontend_stage_name The name for the frontend stage.
 * @param host_ip The host IP address for UDP output.
 * @param skip_stream_predicate Optional predicate to skip certain streams (e.g., AI_SINK for sensor0).
 */
void configure_sensor(int sensor_idx, InstanceResources &instance_resources, const std::string &frontend_stage_name,
                      const std::string &host_ip,
                      std::function<bool(const std::string &)> skip_stream_predicate = nullptr)
{
    std::string medialib_config_string = read_string_from_file(instance_resources.medialib_config_path.c_str());
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library for sensor" << sensor_idx << std::endl;
        throw std::runtime_error("Failed to create media library for sensor" + std::to_string(sensor_idx));
    }
    instance_resources.media_library = media_lib_expected.value();
    if (instance_resources.media_library->initialize(medialib_config_string) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library for sensor" << sensor_idx << std::endl;
        throw std::runtime_error("Failed to initialize media library for sensor" + std::to_string(sensor_idx));
    }
    if (instance_resources.profile_name != NO_PROFILE_SELECTED)
    {
        instance_resources.media_library->set_profile(instance_resources.profile_name);
    }
    // Create and configure frontend
    instance_resources.frontend = hailo_analytics::pipeline::sources::FrontendStageBuild::create()
                                      .set_stage_name(frontend_stage_name)
                                      .set_trace_opt(true)
                                      .buildptr();
    hailo_analytics::pipeline::AppStatus frontend_config_status =
        instance_resources.frontend->configure(instance_resources.media_library->m_frontend);
    if (frontend_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << frontend_stage_name << std::endl;
        throw std::runtime_error("Failed to configure frontend for sensor" + std::to_string(sensor_idx));
    }

    // Get frontend output streams
    auto streams = instance_resources.frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids for sensor" << sensor_idx << std::endl;
        throw std::runtime_error("Failed to get stream ids for sensor" + std::to_string(sensor_idx));
    }

    // Create encoders and UDP outputs for each stream (with optional filtering)
    for (auto s : streams.value())
    {
        if (skip_stream_predicate && skip_stream_predicate(s.id))
        {
            continue;
        }
        create_encoder_and_udp(sensor_idx, s.id, instance_resources, host_ip);
    }
}

/**
 * @brief Configure sensor1 (simple stream flow).
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_sensor1(std::shared_ptr<AppResources> app_resources)
{
    configure_sensor(1, app_resources->sensor1, FRONTEND_STAGE_SENSOR1, app_resources->host_ip);
}

/**
 * @brief Configure sensor0 (AI pipeline flow).
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_sensor0(std::shared_ptr<AppResources> app_resources)
{
    // Skip AI_SINK as it does not get connected directly to an encoder
    configure_sensor(0, app_resources->sensor0, FRONTEND_STAGE_SENSOR0, app_resources->host_ip,
                     [](const std::string &stream_id) { return stream_id == AI_SINK; });
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
void create_main_pipeline(std::shared_ptr<AppResources> app_resources)
{
    try
    {
        // Get the input resolution from frontend for sensor0
        auto streams_sensor0 = app_resources->sensor0.frontend->get_outputs_streams();

        // Retrieve the vision input resolution for sanity check and which we will be using for crop
        int bbox_crop_input_width = 0;
        int bbox_crop_input_height = 0;
        if (streams_sensor0.has_value())
        {
            for (const auto &stream : streams_sensor0.value())
            {
                if (stream.id == VISION_SINK)
                {
                    bbox_crop_input_width = stream.width;
                    bbox_crop_input_height = stream.height;
                    break;
                }
            }
        }

        if (bbox_crop_input_width == 0 || bbox_crop_input_height == 0)
        {
            std::cerr << "Failed to get input resolution from frontend for sensor0" << std::endl;
            throw std::runtime_error("Failed to get input resolution from frontend for sensor0");
        }

        /*
            Configure AI Pipeline using shared builder for sensor0
        */
        ai_example_app::AIPipelineConfig ai_config;
        ai_config.print_fps = app_resources->print_fps;
        ai_config.print_latency = app_resources->print_latency;
        ai_config.input_width = bbox_crop_input_width;
        ai_config.input_height = bbox_crop_input_height;
        ai_config.tiles = TILES;
        ai_config.yolo_hef_file = YOLO_HEF_FILE;
        ai_config.yolo_post_so = YOLO_POST_SO;
        ai_config.yolo_func_name = YOLO_FUNC_NAME;
        ai_config.yolo_post_conf = YOLO_POST_CONF;
        ai_config.landmarks_hef_file = LANDMARKS_HEF_FILE;
        ai_config.landmarks_post_so = LANDMARKS_POST_SO;
        ai_config.landmarks_func_name = LANDMARKS_FUNC_NAME;
        ai_config.draw_overlay_sink0 =
            !app_resources->draw_overlay_streams.empty() &&
            app_resources->draw_overlay_streams.find(0) != app_resources->draw_overlay_streams.end();
        ai_config.draw_overlay_sink1 =
            !app_resources->draw_overlay_streams.empty() &&
            app_resources->draw_overlay_streams.find(1) != app_resources->draw_overlay_streams.end();
        ai_config.full_landmarks = app_resources->full_landmarks;
        ai_config.landmark_indices_to_draw = LANDMARKS_INDICES_EXAMPLE;

        // Create AI pipeline stages using shared builder
        ai_example_app::AIPipelineStages ai_stages = ai_example_app::AIPipelineBuilder::create_ai_stages(ai_config);

        // Set callback function for the callback stage
        ai_example_app::AIPipelineBuilder::set_callback_function(
            ai_stages, [](hailo_analytics::pipeline::BufferPtr data) {
                static int counter = 0;
                static const int threshold = 2; // Toggle every 2 calls
                counter = (counter + 1) % threshold;

                if (counter < threshold / 2)
                {
                    hailo_analytics::pipeline::CroppingMetadataPtr cropping_meta =
                        std::make_shared<hailo_analytics::pipeline::CroppingMetadata>(1);
                    data->add_metadata(cropping_meta);
                }
                else
                {
                    hailo_analytics::pipeline::CroppingMetadataPtr cropping_meta =
                        std::make_shared<hailo_analytics::pipeline::CroppingMetadata>(0);
                    data->add_metadata(cropping_meta);
                }
            });

        /*
            Add stages to pipeline using pipeline builder
        */
        hailo_analytics::pipeline::PipelineBuilder pip_builder;

        // Add sensor0 frontend (AI pipeline)
        pip_builder.add_stage(app_resources->sensor0.frontend, hailo_analytics::pipeline::StageType::SOURCE);

        // Add sensor1 frontend (simple stream)
        pip_builder.add_stage(app_resources->sensor1.frontend, hailo_analytics::pipeline::StageType::SOURCE);

        // Add AI pipeline stages for sensor0
        ai_example_app::AIPipelineBuilder::add_stages_to_pipeline(pip_builder, ai_stages);

        // Add encoder and udp to stage for sensor0 (except AI_SINK)
        for (auto s : streams_sensor0.value())
        {
            // AI_SINK does not get an encoder since it is merged into 4K
            if (s.id != AI_SINK)
            {
                // Add encoder/udp to pipeline stage
                pip_builder.add_stage(app_resources->sensor0.encoders[s.id], hailo_analytics::pipeline::StageType::SINK)
                    .add_stage(app_resources->sensor0.udp_outputs[s.id], hailo_analytics::pipeline::StageType::SINK);
            }
        }

        // Add encoder and udp to stage for sensor1
        auto streams_sensor1 = app_resources->sensor1.frontend->get_outputs_streams();
        if (streams_sensor1.has_value())
        {
            for (auto s : streams_sensor1.value())
            {
                // Add encoder/udp to pipeline stage
                pip_builder.add_stage(app_resources->sensor1.encoders[s.id], hailo_analytics::pipeline::StageType::SINK)
                    .add_stage(app_resources->sensor1.udp_outputs[s.id], hailo_analytics::pipeline::StageType::SINK);

                // Subscribe encoder to frontend
                HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for sensor1 {}", s.id);
                pip_builder.connect_frontend(FRONTEND_STAGE_SENSOR1, s.id,
                                             app_resources->sensor1.encoders[s.id]->get_name());
            }
        }

        /*
            Subscribe stages of the pipeline to each other
        */

        // Sensor0 FrontEnd pipeline stages subscription (AI pipeline)
        for (auto s : streams_sensor0.value())
        {
            if (s.id == AI_SINK)
            {
                HAILO_ANALYTICS_LOG_INFO("subscribing ai pipeline to frontend for sensor0 {}", s.id);
                // Subscribe tiling to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE_SENSOR0, s.id, TILING_STAGE);
            }
            else if (s.id == VISION_SINK || s.id == SECONDARY_VISION_SINK)
            {
                HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for sensor0 {}", s.id);
                // Subscribe tiling aggregator to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE_SENSOR0, s.id, MUXER_STAGE);
            }
            else
            {
                HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for sensor0 {}", s.id);
                // Subscribe encoder to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE_SENSOR0, s.id,
                                             app_resources->sensor0.encoders[s.id]->get_name());
            }
        }

        // Connect AI pipeline stages using shared builder
        ai_example_app::AIPipelineBuilder::connect_ai_stages(pip_builder, ai_stages, ai_config);

        // Connect overlay outputs to encoders
        pip_builder.connect(OVERLAY_STAGE_SINK0, app_resources->sensor0.encoders[VISION_SINK]->get_name())
            .connect(OVERLAY_STAGE_SINK1, app_resources->sensor0.encoders[SECONDARY_VISION_SINK]->get_name());

        // Stream Out pipeline stages for sensor0
        for (auto s : streams_sensor0.value())
        {
            // AI_SINK does not get an encoder since it is merged into 4K
            if (s.id != AI_SINK)
            {
                pip_builder.connect(app_resources->sensor0.encoders[s.id]->get_name(),
                                    app_resources->sensor0.udp_outputs[s.id]->get_name());
            }
        }

        // Stream Out pipeline stages for sensor1
        auto streams_sensor1_out = app_resources->sensor1.frontend->get_outputs_streams();
        if (streams_sensor1_out.has_value())
        {
            for (auto s : streams_sensor1_out.value())
            {
                pip_builder.connect(app_resources->sensor1.encoders[s.id]->get_name(),
                                    app_resources->sensor1.udp_outputs[s.id]->get_name());
            }
        }

        /*
            Build the pipeline
        */
        app_resources->pipeline = pip_builder.build("ai_example_dual_sensor_pipeline", true);
    }
    catch (const std::exception &e)
    {
        HAILO_ANALYTICS_LOG_ERROR("{} failed: {}", __func__, e.what());
    }
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
bool g_stop_requested = false;

int main(int argc, char *argv[])
{
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->sensor0.medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR0;
    app_resources->sensor1.medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR1;

    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([]([[maybe_unused]] int signal) {
        std::cout << "Stopping Pipeline..." << std::endl;
        HAILO_ANALYTICS_LOG_INFO("Stopping Pipeline...");
        std::lock_guard<std::mutex> lock(g_stop_mutex);
        g_stop_requested = true;
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
        case ArgumentType::PrintLatency:
            app_resources->print_latency = true;
            break;
        case ArgumentType::ConfigSensor0:
            app_resources->sensor0.medialib_config_path = result["config-file-path-sensor-0"].as<std::string>();
            break;
        case ArgumentType::ConfigSensor1:
            app_resources->sensor1.medialib_config_path = result["config-file-path-sensor-1"].as<std::string>();
            break;
        case ArgumentType::ProfileSensor0:
            app_resources->sensor0.profile_name = result["profile-sensor-0"].as<std::string>();
            break;
        case ArgumentType::ProfileSensor1:
            app_resources->sensor1.profile_name = result["profile-sensor-1"].as<std::string>();
            break;
        case ArgumentType::DrawOverlay: {
            std::string overlay_streams_str = result["draw-overlay"].as<std::string>();
            if (!overlay_streams_str.empty())
            {
                app_resources->draw_overlay_streams.clear();
                std::stringstream ss(overlay_streams_str);
                std::string token;
                while (std::getline(ss, token, ','))
                {
                    try
                    {
                        uint stream_num = std::stoul(token);
                        app_resources->draw_overlay_streams.insert(stream_num);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "Error: Invalid stream number: " << token << std::endl;
                        return 1;
                    }
                }
            }
        }
        break;
        case ArgumentType::FullLandmarks:
            app_resources->full_landmarks = true;
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    // Update sensor0 config path if not explicitly set
    // Only change if user didn't provide a custom config path
    if (!result.count("config-file-path-sensor-0"))
    {
        app_resources->sensor0.medialib_config_path = MEDIALIB_CONFIG_PATH_SENSOR0;
        std::cout << "Using dual sensor config for sensor0: " << app_resources->sensor0.medialib_config_path
                  << std::endl;
    }
    else
    {
        std::cout << "Using custom config for sensor0: " << app_resources->sensor0.medialib_config_path << std::endl;
    }

    setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);

    // Configure frontend and encoders for sensor0
    configure_sensor0(app_resources);

    // Configure sensor1
    configure_sensor1(app_resources);

    // Create pipeline and stages
    create_main_pipeline(app_resources);

    // Start pipeline
    std::cout << "Starting." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Starting.");
    app_resources->pipeline->start();

    HAILO_ANALYTICS_LOG_INFO("Started playing for {} seconds.", timeout);

    // Wait for either timeout or signal
    std::unique_lock<std::mutex> lk(g_stop_mutex);
    if (!g_stop_requested)
    {
        g_stop_cv.wait_for(lk, std::chrono::seconds(timeout), [] { return g_stop_requested; });
    }
    else
    {
        // If stop was requested during startup, wait a bit for pipeline to settle before stopping
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    HAILO_ANALYTICS_LOG_INFO("Stopping.");
    app_resources->pipeline->stop();
    return 0;
}
