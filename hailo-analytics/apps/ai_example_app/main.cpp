// general includes
#include <queue>
#include <set>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <tl/expected.hpp>
#include <signal.h>
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
#include "constants.hpp"
#include "ai_pipeline_builder.hpp"

// Frontend Params
#define FRONTEND_STAGE "frontend_stage"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/ai_example_medialib_config.json"

// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    Profile,
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
    ("c,config-file-path", "Media library configuration path", 
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("a,profile", "Profile name", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("d,draw-overlay", "Comma-separated list of stream numbers to draw overlay on (e.g., '0,1,2...'), will ignore any non-existing streams", 
        cxxopts::value<std::string>()->default_value("1"))
    ("f,full-landmarks", "Draw all landmarks (default draws only eyes for face landmarks)", 
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

    if (result.count("config-file-path"))
    {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("profile"))
    {
        arguments.push_back(ArgumentType::Profile);
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
 * @brief Holds the resources required for the application.
 *
 * This structure contains pointers to various components and modules
 * used by the application, including the frontend, encoders, UDP outputs,
 * and the pipeline. It also includes a flag to control whether FPS (frames per second)
 * information should be printed.
 */
struct AppResources
{
    std::shared_ptr<MediaLibrary> media_library;
    std::shared_ptr<hailo_analytics::pipeline::sources::FrontendStage> frontend;
    std::map<output_stream_id_t, std::shared_ptr<hailo_analytics::pipeline::codecs::EncoderStage>> encoders;
    std::map<output_stream_id_t, std::shared_ptr<hailo_analytics::pipeline::sinks::UdpStage>> udp_outputs;
    hailo_analytics::pipeline::PipelinePtr pipeline;
    bool print_fps;
    bool print_latency;
    std::set<uint> draw_overlay_streams = {1};
    bool full_landmarks;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string restricted_profile_name;

    void clear()
    {
        frontend = nullptr;
        pipeline = nullptr;
        encoders.clear();
        udp_outputs.clear();
        print_fps = false;
        print_latency = false;
        draw_overlay_streams.clear();
        full_landmarks = false;
        medialib_config_path = "";
        media_library = nullptr;
        profile_name = NO_PROFILE_SELECTED;
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
 * @brief Create and configure an encoder and its corresponding UDP output file.
 *
 * This function sets up an encoder and a UDP output module for a given stream ID.
 * It reads configuration files and initializes the encoder and UDP module accordingly.
 *
 * @param id The ID of the output stream.
 * @param app_resources Shared pointer to the application's resources.
 */
void create_encoder_and_udp(const std::string &id, std::shared_ptr<AppResources> app_resources)
{
    // Create and configure encoder
    std::string enc_name = "enc_" + id;
    std::cout << "Creating encoder " << enc_name << std::endl;
    std::shared_ptr<hailo_analytics::pipeline::codecs::EncoderStage> encoder_stage =
        hailo_analytics::pipeline::codecs::EncoderStageBuild::create().set_stage_name(enc_name).buildptr();

    app_resources->encoders[id] = encoder_stage;
    hailo_analytics::pipeline::AppStatus enc_config_status =
        encoder_stage->configure(app_resources->media_library->m_encoders[id]);
    if (enc_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder " << enc_name << std::endl;
        throw std::runtime_error("Failed to configure encoder");
    }

    // Create and conifgure udp
    std::string udp_name = "udp_" + id;
    std::cout << "Creating udp " << udp_name << std::endl;
    std::shared_ptr<hailo_analytics::pipeline::sinks::UdpStage> udp_stage =
        hailo_analytics::pipeline::sinks::UdpStageBuild::create()
            .set_stage_name(udp_name)
            .set_leaky_opt(false)
            .set_trace_opt(true)
            .set_printfps_opt(true)
            .buildptr();
    app_resources->udp_outputs[id] = udp_stage;
    hailo_analytics::pipeline::AppStatus udp_config_status = udp_stage->configure(
        app_resources->host_ip, PORT_FROM_ID(id), hailo_analytics::pipeline::sinks::EncodingType::H264);
    if (udp_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }
}

void configure_profile_restriction_handlers(std::shared_ptr<AppResources> app_resources)
{
    app_resources->media_library->subscribe_to_profile_restricted(
        [app_resources](const config_profile_t &previous_profile, const config_profile_t &new_profile) {
            HAILO_ANALYTICS_LOG_WARN("Profile {} restricted due to thermal conditions, fallback profile {} activated.",
                                     previous_profile.name, new_profile.name);
            // Store the restricted profile name to restore it later
            app_resources->restricted_profile_name = previous_profile.name;
            app_resources->profile_name = new_profile.name;
        });

    app_resources->media_library->subscribe_to_profile_restriction_done([app_resources]() {
        if (!app_resources->restricted_profile_name.empty())
        {
            HAILO_ANALYTICS_LOG_WARN("Profile restriction is done, Restoring profile to: {}",
                                     app_resources->restricted_profile_name);
            if (app_resources->media_library->set_profile(app_resources->restricted_profile_name) !=
                media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to restore profile {}", app_resources->restricted_profile_name);
            }
            else
            {
                HAILO_ANALYTICS_LOG_DEBUG("Profile restored to: {} Successfully",
                                          app_resources->restricted_profile_name);
                app_resources->profile_name = app_resources->restricted_profile_name;
            }
            // Clear the restricted profile name after restoration
            app_resources->restricted_profile_name.clear();
        }
    });
}

/**
 * @brief Configure the frontend and encoders for the application.
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_frontend_and_encoders(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_string = read_string_from_file(app_resources->medialib_config_path.c_str());
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();

    configure_profile_restriction_handlers(app_resources);

    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        throw std::runtime_error("Failed to initialize media library");
    }
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        HAILO_ANALYTICS_LOG_INFO("Setting media library profile to {}", app_resources->profile_name);
        std::string profile_to_set = app_resources->profile_name;
        media_library_return set_profile_ret = app_resources->media_library->set_profile(profile_to_set);
        if (set_profile_ret == media_library_return::MEDIA_LIBRARY_PROFILE_IS_RESTRICTED)
        {
            HAILO_ANALYTICS_LOG_WARN("Profile {} is restricted due to thermal conditions", profile_to_set);
        }
        else if (set_profile_ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to set profile to {}", profile_to_set);
            throw std::runtime_error("Failed to set profile");
        }
    }
    // Create and configure frontend
    app_resources->frontend =
        hailo_analytics::pipeline::sources::FrontendStageBuild::create().set_stage_name(FRONTEND_STAGE).buildptr();
    hailo_analytics::pipeline::AppStatus frontend_config_status =
        app_resources->frontend->configure(app_resources->media_library->m_frontend);
    if (frontend_config_status != hailo_analytics::pipeline::AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << FRONTEND_STAGE << std::endl;
        throw std::runtime_error("Failed to configure frontend");
    }

    // Get frontend output streams
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    // Create encoders and output files for each stream
    for (auto s : streams.value())
    {
        if (s.id == AI_SINK)
        {
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }
        create_encoder_and_udp(s.id, app_resources);
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
void create_main_pipeline(std::shared_ptr<AppResources> app_resources)
{
    try
    {
        // Get the input resolution from frontend
        auto streams = app_resources->frontend->get_outputs_streams();

        // Retrieve the vision input resolution for sanity check and which we will be using for crop
        int bbox_crop_input_width = 0;
        int bbox_crop_input_height = 0;
        if (streams.has_value())
        {
            for (const auto &stream : streams.value())
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
            std::cerr << "Failed to get input resolution from frontend" << std::endl;
            throw std::runtime_error("Failed to get input resolution from frontend");
        }

        /*
            Configure AI Pipeline using shared builder
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
                // Check if sink2 (AI_SINK) was acquired alongside this buffer
                // This indicates that both the 30fps stream (sink0) and 15fps stream (sink1) got buffers together
                bool has_matching_pair = data->get_buffer()->concurrent_stream_ids.count(AI_SINK) > 0;
                hailo_analytics::pipeline::CroppingMetadataPtr cropping_meta =
                    std::make_shared<hailo_analytics::pipeline::CroppingMetadata>(has_matching_pair);
                data->add_metadata(cropping_meta);
            });

        /*
            Add stages to pipeline using pipeline builder
        */
        hailo_analytics::pipeline::PipelineBuilder pip_builder;

        // Add frontend
        pip_builder.add_stage(app_resources->frontend, hailo_analytics::pipeline::StageType::SOURCE);

        // Add AI pipeline stages
        ai_example_app::AIPipelineBuilder::add_stages_to_pipeline(pip_builder, ai_stages);

        // Add encoder and udp to stage (except AI_SINK)
        for (auto s : streams.value())
        {
            // AI_SINK does not get an encoder since it is merged into 4K
            if (s.id != AI_SINK)
            {
                // Add encoder/udp to pipeline stage
                pip_builder.add_stage(app_resources->encoders[s.id], hailo_analytics::pipeline::StageType::SINK)
                    .add_stage(app_resources->udp_outputs[s.id], hailo_analytics::pipeline::StageType::SINK);
            }
        }

        /*
            Subscribe stages of the pipeline to each other
        */

        // FrontEnd pipeline stages subscription
        for (auto s : streams.value())
        {
            if (s.id == AI_SINK)
            {
                HAILO_ANALYTICS_LOG_INFO("subscribing ai pipeline to frontend for {}", s.id);
                // Subscribe tiling to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE, s.id, TILING_STAGE);
            }
            else if (s.id == VISION_SINK || s.id == SECONDARY_VISION_SINK)
            {
                HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for {}", s.id);
                // Subscribe tiling aggregator to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE, s.id, MUXER_STAGE);
            }
            else
            {
                HAILO_ANALYTICS_LOG_INFO("subscribing to frontend for {}", s.id);
                // Subscribe encoder to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE, s.id, app_resources->encoders[s.id]->get_name());
            }
        }

        // Connect AI pipeline stages using shared builder
        ai_example_app::AIPipelineBuilder::connect_ai_stages(pip_builder, ai_stages, ai_config);

        // Connect overlay outputs to encoders
        pip_builder.connect(OVERLAY_STAGE_SINK0, app_resources->encoders[VISION_SINK]->get_name())
            .connect(OVERLAY_STAGE_SINK1, app_resources->encoders[SECONDARY_VISION_SINK]->get_name());

        // Stream Out pipeline stages
        for (auto s : streams.value())
        {
            // AI_SINK does not get an encoder since it is merged into 4K
            if (s.id != AI_SINK)
            {
                pip_builder.connect(app_resources->encoders[s.id]->get_name(),
                                    app_resources->udp_outputs[s.id]->get_name());
            }
        }

        /*
            Build the pipeline
        */
        app_resources->pipeline = pip_builder.build("ai_example_pipeline", true);
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
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

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
        case ArgumentType::Config:
            app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
            break;
        case ArgumentType::Profile:
            app_resources->profile_name = result["profile"].as<std::string>();
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

    setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);

    // Configure frontend and encoders
    configure_frontend_and_encoders(app_resources);

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
