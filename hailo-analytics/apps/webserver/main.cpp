#include <cxxopts/cxxopts.hpp>
#include <httplib.h> // IWYU pragma: keep
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <filesystem>
#include <optional>
#include <csignal>
#include <cctype>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include "media_library/cloexec_fstream.hpp"
#include "pipeline/pipeline_factory.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "common/common.hpp"
#include "resources/common/repository.hpp"
#include "media_library/signal_utils.hpp"
#include "media_library/media_library_types.hpp"
#include "pipeline/pipeline.hpp"

#define DEFAULT_CONFIGS_PATH "/etc/imaging/cfg/medialib_configs/"
#define APPEND_CONFIG_PATH(path) DEFAULT_CONFIGS_PATH path
#define DEFAULT_MEDIALIB_CONFIG_PATH APPEND_CONFIG_PATH("webserver_medialib_config.json")

static std::atomic<bool> g_webserver_stopping{false};

struct DeviceUser
{
    pid_t pid;
    std::string process_name;
};

std::optional<DeviceUser> find_device_user(const std::string &device_path)
{
    struct stat dev_stat;
    if (stat(device_path.c_str(), &dev_stat) != 0)
        return std::nullopt;

    pid_t self = getpid();
    std::error_code ec;

    for (const auto &proc : std::filesystem::directory_iterator("/proc", ec))
    {
        const auto &path = proc.path();
        const auto name = path.filename().string();

        if (name.empty() || !std::isdigit(name[0]))
            continue;

        pid_t pid = std::stoi(name);
        if (pid == self)
            continue;

        std::string comm;
        if (cloexec::ifstream(path / "comm") >> comm && comm.rfind("isp_med", 0) == 0)
            continue;

        for (const auto &fd : std::filesystem::directory_iterator(path / "fd", ec))
        {
            struct stat fd_stat;
            if (stat(fd.path().c_str(), &fd_stat) == 0 && S_ISCHR(fd_stat.st_mode) &&
                fd_stat.st_rdev == dev_stat.st_rdev)
            {
                // Read full process name from cmdline (comm is truncated to 15 chars)
                std::string full_name = comm;
                std::string cmdline;
                if (cloexec::ifstream cmdline_file(path / "cmdline"); cmdline_file)
                {
                    std::getline(cmdline_file, cmdline, '\0');
                    if (!cmdline.empty())
                        full_name = std::filesystem::path(cmdline).filename().string();
                }
                return DeviceUser{pid, full_name};
            }
        }
    }
    return std::nullopt;
}
void flags_init(int argc, char *argv[], std::string &medialib_config_path)
{
    try
    {
        cxxopts::Options options(argv[0], "Webserver application");
        options.add_options()("config", "Media library configuration path",
                              cxxopts::value<std::string>()->default_value(DEFAULT_MEDIALIB_CONFIG_PATH))(
            "h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        std::string config_path = result["config"].as<std::string>();
        WEBSERVER_LOG_INFO("Using medialib config path: {}", config_path);
        medialib_config_path = config_path;
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
    WEBSERVER_LOG_INFO("Starting webserver");

    auto device_user = find_device_user("/dev/video0");
    if (device_user)
    {
        std::cout << "\nWARNING: /dev/video0 is already in use by '" << device_user->process_name << "' (PID "
                  << device_user->pid << ").\nAuto terminating it before proceeding, please wait...\n"
                  << std::endl;

        WEBSERVER_LOG_INFO("Auto terminating conflicting app - Sending SIGTERM to '{}' (PID {})",
                           device_user->process_name, device_user->pid);
        kill(device_user->pid, SIGTERM);

        static constexpr int MAX_WAIT_MS = 5000;
        static constexpr int POLL_INTERVAL_US = 100000; // 100ms
        int waited_ms = 0;
        while (waited_ms < MAX_WAIT_MS)
        {
            if (kill(device_user->pid, 0) != 0)
                break;
            usleep(POLL_INTERVAL_US);
            waited_ms += POLL_INTERVAL_US / 1000;
        }

        if (kill(device_user->pid, 0) == 0)
        {
            WEBSERVER_LOG_INFO("Process '{}' (PID {}) did not exit after SIGTERM, sending SIGKILL",
                               device_user->process_name, device_user->pid);
            kill(device_user->pid, SIGKILL);
        }
    }

    setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);
    setenv("MEDIALIB_FD_DUP", "1", 1);

    // Disable persistence so analytics config gets updated when switching pipelines
    application_analytics_config_t::is_persistent = false;

    std::string medialib_config_path = "";
    Architecture arch = get_hailo_architecture();
    flags_init(argc, argv, medialib_config_path);

    std::unique_ptr<HTTPServer> svr = HTTPServer::create();
    // register error handler
    svr->set_exception_handler([](const auto & /*req*/, auto &res, std::exception_ptr ep) {
        auto fmt = "Error 500: %s";
        char buf[BUFSIZ];
        try
        {
            std::rethrow_exception(ep);
        }
        catch (std::exception &e)
        {
            snprintf(buf, sizeof(buf), fmt, e.what());
            WEBSERVER_LOG_ERROR("{}", buf);
        }
        catch (...)
        { // See the following NOTE
            snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
            WEBSERVER_LOG_ERROR("Unknown Excpetion");
        }
        res.set_content(buf, "text/html");
        res.status = 500;
    });

    // Create pipeline factory instead of direct pipeline creation
    WebserverResourceRepository resources =
        webserver::resources::ResourceRepository::create(*svr.get(), medialib_config_path);

    auto pipeline_factory = std::make_unique<webserver::pipeline::PipelineFactory>(*resources, arch, pipeline_t::Basic);

    signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([&pipeline_factory, &resources, &svr](int signal) {
        // Use an atomic flag to make sure the shutdown sequence is executed only once,
        // even if multiple signals are delivered or multiple threads enter this handler.
        bool expected = false;

        // On the first call:
        //   - g_webserver_stopping is false  -> compare_exchange_strong() returns true
        //   - g_webserver_stopping is set to true and the shutdown flow continues.
        // On any subsequent call:
        //   - g_webserver_stopping is already true
        //   - compare_exchange_strong() returns false and we exit the handler immediately.
        if (!g_webserver_stopping.compare_exchange_strong(expected, true))
        {
            return;
        }
        std::cout << "Received signal " << signal << ", stopping pipeline..." << std::endl;

        pipeline_factory = nullptr;
        resources = nullptr;
        svr = nullptr;

        std::cout << "Pipeline stopped, exiting now" << std::endl;
        // Use _Exit(0) to terminate the process immediately without running static
        // destructors, flushing stdio buffers, or calling any atexit/quick_exit handlers.
        _Exit(0);
    });

    pipeline_factory->get_current_pipeline()->start();

    WEBSERVER_LOG_INFO("Webserver started");
    svr->listen("0.0.0.0", 80);
}
