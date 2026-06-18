#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/sysinfo.h>
#include <thread>

#include "media_library/signal_utils.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

#include "apps/vlm_pipeline_defines.hpp"
#include "apps/webserver.hpp"
#include "utils/vlm_app_config_parser.hpp"

namespace
{
std::function<void(int)> g_signal_callback;

void signal_handler_func(int signal)
{
    if (g_signal_callback)
    {
        g_signal_callback(signal);
    }
}

// Threshold is set to 7 GB rather than exactly 8 because boards report
// `totalram` slightly below the nominal stick size (kernel reservations);
constexpr double kMinSystemMemoryGB = 7.0;

double total_memory_gb()
{
    struct sysinfo info{};
    if (sysinfo(&info) != 0)
    {
        return 0.0;
    }
    return (static_cast<double>(info.totalram) * info.mem_unit) / (1024.0 * 1024.0 * 1024.0);
}
} // namespace

int main()
{
    vlm_app_config::VlmAppConfigParser config_parser;
    if (!config_parser.parse_from_file(vlm_app::paths::vlm_app_config))
    {
        HAILO_ANALYTICS_LOG_ERROR("Unable to load {}", vlm_app::paths::vlm_app_config);
        return -1;
    }

    const vlm_app_config::VlmAppConfig config = config_parser.get_config();

    // ── Pre-flight check: total system RAM ──────────────────────────────
    const double mem_gb = total_memory_gb();
    if (mem_gb < kMinSystemMemoryGB)
    {
        std::cout << "\n"
                  << "================================================================\n"
                  << "  VLM Event Monitor — Startup Error\n"
                  << "================================================================\n"
                  << "\n"
                  << "  Insufficient system memory.\n"
                  << "\n"
                  << "  This application requires at least 8 GB of system RAM.\n"
                  << "  Detected: " << std::fixed << std::setprecision(2) << mem_gb << " GB\n"
                  << "\n"
                  << "================================================================\n"
                  << std::endl;
        return -1;
    }

    // ── Pre-flight check: required HEF file exists ──────────────────────
    {
        const std::string &hef_path = config.vlm_model.hef_path;
        if (hef_path.empty() || !std::filesystem::exists(hef_path))
        {
            const std::string hef_basename = std::filesystem::path(hef_path).filename().string();
            const std::string display_name = hef_basename.empty() ? "the configured HEF" : hef_basename;
            std::cout << "\n"
                      << "================================================================\n"
                      << "  VLM Event Monitor — Startup Error\n"
                      << "================================================================\n"
                      << "\n"
                      << "  Required VLM HEF model not found.\n"
                      << "\n"
                      << "  Expected at:\n"
                      << "    " << hef_path << "\n"
                      << "\n"
                      << "  Download '" << display_name << "' from the Hailo Gen-AI Model Zoo:\n"
                      << "    https://github.com/hailo-ai/hailo_model_zoo_genai/blob/main/docs/MODELS.rst\n"
                      << "\n"
                      << "  Place it at the path above, or update vlm_model.hef_path in\n"
                      << "  vlm_app_config.yaml.\n"
                      << "\n"
                      << "================================================================\n"
                      << std::endl;
            return -1;
        }
    }

    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler(signal_handler_func);

    auto server_result = IntegratedWebServer::create(config);
    if (!server_result)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to instantiate IntegratedWebServer: {}", server_result.error());
        return -1;
    }
    std::shared_ptr<IntegratedWebServer> server = server_result.value();

    g_signal_callback = [&server](int /*sig*/) {
        HAILO_ANALYTICS_LOG_INFO("Stopping VLM Event Monitor...");
        server->stop();
        std::exit(0);
    };

    HAILO_ANALYTICS_LOG_INFO("VLM Event Monitor starting...");

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

    HAILO_ANALYTICS_LOG_INFO("Stopping VLM Event Monitor...");
    server->stop();

    if (server_thread.joinable())
    {
        server_thread.join();
    }

    return 0;
}
