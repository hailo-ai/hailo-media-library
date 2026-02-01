#include <chrono>
#include <iostream>
#include <thread>
#include <signal.h>
#include <nlohmann/json.hpp>
#include <cxxopts/cxxopts.hpp>
#include <atomic>
#include "pipeline/pipeline.hpp"
#include "pipeline/pipeline_factory.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "common/common.hpp"
#include "resources/common/repository.hpp"
#include "resources/common/events_utils.hpp"
#include "media_library/signal_utils.hpp"

#define DEFAULT_CONFIGS_PATH "/etc/imaging/cfg/medialib_configs/"
#define APPEND_CONFIG_PATH(path) DEFAULT_CONFIGS_PATH path
#define DEFAULT_MEDIALIB_CONFIG_PATH APPEND_CONFIG_PATH("webserver_medialib_config.json")

using namespace webserver;
using namespace webserver::pipeline;
using namespace webserver::resources;

// Global flag to handle graceful shutdown
std::atomic<bool> g_profile_manager_stopping(false);

void flags_init(int argc, char *argv[], std::string &medialib_config_path, pipeline_t &initial_pipeline,
                bool &tuning_mode)
{
    try
    {
        cxxopts::Options options(argv[0], "Profile Manager Viewer - Multi-stream pipeline for profile tuning");
        options.add_options()("config", "Media library configuration path",
                              cxxopts::value<std::string>()->default_value(DEFAULT_MEDIALIB_CONFIG_PATH))(
            "tuning-mode", "Activate ProfileManager pipeline for Tuning Tool (HTTP control, multi-stream)",
            cxxopts::value<bool>()->default_value("false"))("h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            std::cout << "\nModes:" << std::endl;
            std::cout << "  --tuning-mode           : Activate Profile Manager pipeline. Supports HTTP control and "
                         "multi-stream."
                      << std::endl;
            std::cout << "  (No flag)               : Standard mode using Basic pipeline." << std::endl;
            std::cout << "\nConfiguration:" << std::endl;
            std::cout
                << "  --config <path>         : Specify Media Library JSON config (optional, defaults to system path)."
                << std::endl;
            exit(0);
        }

        std::string config_path = result["config"].as<std::string>();
        WEBSERVER_LOG_INFO("Using medialib config path: {}", config_path);
        medialib_config_path = config_path;

        tuning_mode = result["tuning-mode"].as<bool>();

        if (tuning_mode)
        {
            // Tuning mode implies ProfileManager capabilities (dynamic streams)
            initial_pipeline = pipeline_t::ProfileManager;
            WEBSERVER_LOG_INFO("Tuning Mode enabled (ProfileManager)");
            std::cout << "Tuning Mode enabled: Using ProfileManager pipeline" << std::endl;
        }
        else
        {
            initial_pipeline = pipeline_t::Basic;
            WEBSERVER_LOG_INFO("Using Basic pipeline (default)");
        }
    }
    catch (const cxxopts::OptionException &e)
    {
        WEBSERVER_LOG_ERROR("Error parsing options: {}", e.what());
        std::cout << "Error parsing options: " << e.what() << std::endl;
        std::cout << "Use --help to see valid options" << std::endl;
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    WEBSERVER_LOG_INFO("Starting Profile Manager Viewer");

    std::string medialib_config_path = "";
    pipeline_t initial_pipeline = pipeline_t::Basic; // Default to Basic
    bool tuning_mode = false;
    Architecture arch = get_hailo_architecture();

    flags_init(argc, argv, medialib_config_path, initial_pipeline, tuning_mode);

    // Create HTTP server
    std::shared_ptr<HTTPServer> svr = HTTPServer::create();

    // Set up exception handler for HTTP server
    svr->set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
        auto fmt = "<h1>Error 500</h1><p>%s</p>";
        char buf[BUFSIZ];
        try
        {
            std::rethrow_exception(ep);
        }
        catch (std::exception &e)
        {
            snprintf(buf, sizeof(buf), fmt, e.what());
            WEBSERVER_LOG_ERROR("HTTP Exception: {}", e.what());
        }
        catch (...)
        {
            snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
            WEBSERVER_LOG_ERROR("Unknown HTTP Exception");
        }
        res.set_content(buf, "text/html");
        res.status = 500;
    });

    // Create resources repository
    WEBSERVER_LOG_INFO("Creating resource repository with config: {}", medialib_config_path);
    WebserverResourceRepository resources = ResourceRepository::create(svr, medialib_config_path);

    if (!resources)
    {
        std::cerr << "CRITICAL ERROR: Failed to create ResourceRepository!" << std::endl;
        return 1;
    }

    std::shared_ptr<BasePipeline> active_pipeline;
    std::shared_ptr<PipelineFactory> pipeline_factory;

    if (tuning_mode)
    {
        std::cout << ">>> TUNING MODE ACTIVATED: Using ProfileManagerPipeline <<<" << std::endl;
    }

    // Unified initialization logic
    // 'initial_pipeline' is already set correctly by flags_init (Basic or ProfileManager)
    WEBSERVER_LOG_INFO("Creating pipeline factory with pipeline type: {}", static_cast<int>(initial_pipeline));

    try
    {
        // We use the Factory for all cases to ensure correct construction
        pipeline_factory = std::make_shared<PipelineFactory>(resources, arch, initial_pipeline);
        active_pipeline = pipeline_factory->get_current_pipeline();
    }
    catch (const std::exception &e)
    {
        std::cerr << "CRASH during Pipeline Factory creation: " << e.what() << std::endl;
        WEBSERVER_LOG_ERROR("CRASH during Pipeline Factory creation: {}", e.what());
        return 1;
    }

    if (!active_pipeline)
    {
        std::cerr << "CRITICAL ERROR: Pipeline Factory returned null pipeline!" << std::endl;
        WEBSERVER_LOG_ERROR("Pipeline Factory returned null pipeline");
        return 1;
    }

    // Register signal handler for graceful shutdown
    signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([active_pipeline](int signal) {
        // Use atomic flag to ensure shutdown sequence runs only once
        bool expected = false;
        if (!g_profile_manager_stopping.compare_exchange_strong(expected, true))
        {
            return;
        }

        std::cout << "\nReceived signal " << signal << ", stopping Profile Manager Viewer..." << std::endl;
        WEBSERVER_LOG_INFO("Received signal {}, initiating shutdown", signal);

        if (active_pipeline)
        {
            WEBSERVER_LOG_INFO("Stopping pipeline");
            active_pipeline->stop();
        }

        WEBSERVER_LOG_INFO("Profile Manager Viewer stopped, exiting");
        std::cout << "Profile Manager Viewer stopped successfully" << std::endl;

        // Exit immediately without running destructors to avoid segfaults on exit
        _Exit(0);
    });

    // Start the pipeline
    WEBSERVER_LOG_INFO("Starting pipeline");
    try
    {
        if (active_pipeline)
        {
            active_pipeline->start();
            WEBSERVER_LOG_INFO("Pipeline started successfully");
        }
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to start pipeline: {}", e.what());
        std::cerr << "ERROR: Failed to start pipeline: " << e.what() << std::endl;
        _Exit(1);
    }

    // Start HTTP server
    WEBSERVER_LOG_INFO("Profile Manager Viewer webserver starting on port 80");
    std::cout << "Profile Manager Viewer is running" << std::endl;
    std::cout << "  - HTTP server: http://0.0.0.0:80" << std::endl;

    if (tuning_mode)
    {
        std::cout << "  - Pipeline: ProfileManager (TUNING MODE)" << std::endl;
    }
    else
    {
        std::cout << "  - Pipeline: Basic (default)" << std::endl;
    }

    std::cout << "  - Config: " << medialib_config_path << std::endl;
    std::cout << "\nPress Ctrl+C to stop..." << std::endl;

    svr->listen("0.0.0.0", 80);

    WEBSERVER_LOG_INFO("HTTP server stopped");
    return 0;
}
