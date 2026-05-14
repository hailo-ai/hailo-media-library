
// general includes
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "media_library/signal_utils.hpp"
#include "clip_app_config_parser.hpp"
#include "clip_pipeline_ai.hpp"
#include "webserver.hpp"
#include "common_utils.hpp"
#include "clip_pipeline_ai_defines.hpp"

std::function<void(int)> g_signal_callback;

void signal_handler_func(int signal)
{
    if (g_signal_callback)
    {
        g_signal_callback(signal);
    }
}

int main()
{

    ClipAppConfigParser config_parser;
    if (!config_parser.parse_from_file(app::paths::clip_app_config))
    {
        HAILO_ANALYTICS_LOG_ERROR("Unable to load {}", app::paths::clip_app_config);
        return -1;
    }

    ClipAppConfig config = config_parser.get_config();

    if (!ClipVideoPipeline::is_supported(config.storage_config))
    {
        HAILO_ANALYTICS_LOG_ERROR("System does not meet ClipVideoPipeline requirements");
        return -1;
    }

    // register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler(signal_handler_func);

    // Start Application Server
    auto server_result = IntegratedWebServer::create(config);
    if (!server_result)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to instantiate IntegratedWebServer, {}", server_result.error());
        return -1;
    }

    std::shared_ptr<IntegratedWebServer> server = server_result.value();

    g_signal_callback = [&server](int /*sig*/) {
        HAILO_ANALYTICS_LOG_INFO("Stopping Pipeline...");
        // Stop Application
        server->stop();
        // terminate program
        exit(0);
    };

    HAILO_ANALYTICS_LOG_INFO("Application starting...");

    std::thread server_thread(
        [&server, &config]() { server->start(config.server_info.host, config.server_info.port); });

    std::string command;
    while (true)
    {
        HAILO_ANALYTICS_LOG_INFO("Enter command (quit): ");
        std::cin >> command;

        if (command == "quit")
        {
            break;
        }
    }

    HAILO_ANALYTICS_LOG_INFO("Stopping Application...");
    server->stop();

    if (server_thread.joinable())
    {
        server_thread.join();
    }

    return 0;
}
