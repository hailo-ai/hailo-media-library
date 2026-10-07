#include <cxxopts/cxxopts.hpp>
#include <httplib.h> // IWYU pragma: keep
#include <sys/stat.h>
#include <sys/wait.h>
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

static constexpr const char *SENSOR_DEVICE_PATH = "/dev/video0";
static constexpr const char *MEDIA_LIBRARY_SERVICE_PROCESS_NAME = "media_library_service";
static constexpr const char *MEDIA_LIBRARY_SERVICE_INIT_SCRIPT = "/etc/init.d/media-library-service.sh";

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
void flags_init(int argc, char *argv[], std::string &medialib_config_path, bool &use_medialib_service)
{
    try
    {
        cxxopts::Options options(argv[0], "Webserver application");
        options.add_options()("config", "Media library configuration path",
                              cxxopts::value<std::string>()->default_value(DEFAULT_MEDIALIB_CONFIG_PATH))(
            "use-medialib-service",
            "Connect to media_library_service instead of hosting the media library in this process",
            cxxopts::value<bool>()->default_value("false"))("h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        std::string config_path = result["config"].as<std::string>();
        WEBSERVER_LOG_INFO("Using medialib config path: {}", config_path);
        medialib_config_path = config_path;

        use_medialib_service = result["use-medialib-service"].as<bool>();
        WEBSERVER_LOG_INFO("Media library mode: {}",
                           use_medialib_service ? "multi process (client/service)" : "single process");
    }
    catch (const cxxopts::OptionException &e)
    {
        WEBSERVER_LOG_ERROR("Error parsing options: {}", e.what());
        std::cout << "Error parsing options: " << e.what() << std::endl;
        std::cout << "Use --help to see valid options" << std::endl;
        exit(1);
    }
}

bool wait_for_process_exit(pid_t pid)
{
    static constexpr int MAX_WAIT_MS = 5000;
    static constexpr int POLL_INTERVAL_US = 100000; // 100ms
    int waited_ms = 0;
    while (waited_ms < MAX_WAIT_MS)
    {
        if (kill(pid, 0) != 0)
            return true;
        usleep(POLL_INTERVAL_US);
        waited_ms += POLL_INTERVAL_US / 1000;
    }
    return kill(pid, 0) != 0;
}

/**
 * @brief Ask the media library service to stop through its init script.
 *
 * Signalling the daemon directly is not enough: the init script runs it under a respawn
 * loop that would restart it and reclaim the sensor. Only the script's stop stops the
 * wrapper too. It sends signals without waiting, so the caller still has to wait for exit.
 *
 * @return false if the script is missing or reported failure, leaving the caller to fall
 *         back to signalling the process.
 */
bool stop_media_library_service()
{
    // Reported by the child when exec itself fails, so "script not installed" can be told
    // apart from "the script ran and failed". Checking the path up front instead would be
    // a time-of-check-to-time-of-use race and would not make the exec any safer.
    static constexpr int EXEC_FAILED_STATUS = 127;

    WEBSERVER_LOG_INFO("Stopping {} via {}", MEDIA_LIBRARY_SERVICE_PROCESS_NAME, MEDIA_LIBRARY_SERVICE_INIT_SCRIPT);
    pid_t child = fork();
    if (child < 0)
    {
        WEBSERVER_LOG_ERROR("Failed to fork to run {}", MEDIA_LIBRARY_SERVICE_INIT_SCRIPT);
        return false;
    }
    if (child == 0)
    {
        // Fixed environment rather than the inherited one: this runs a shell script as
        // root, and the script only needs a PATH to find start-stop-daemon, pkill and rm.
        char *const script_environment[] = {const_cast<char *>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"), nullptr};
        execle(MEDIA_LIBRARY_SERVICE_INIT_SCRIPT, MEDIA_LIBRARY_SERVICE_INIT_SCRIPT, "stop",
               static_cast<char *>(nullptr), script_environment);
        _exit(EXEC_FAILED_STATUS);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0)
    {
        WEBSERVER_LOG_ERROR("Failed to wait for {}", MEDIA_LIBRARY_SERVICE_INIT_SCRIPT);
        return false;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == EXEC_FAILED_STATUS)
    {
        WEBSERVER_LOG_WARNING("{} could not be run; falling back to signalling the process",
                              MEDIA_LIBRARY_SERVICE_INIT_SCRIPT);
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        WEBSERVER_LOG_WARNING("{} stop did not succeed", MEDIA_LIBRARY_SERVICE_INIT_SCRIPT);
        return false;
    }
    return true;
}

void terminate_conflicting_device_user(const std::string &device_path)
{
    auto device_user = find_device_user(device_path);
    if (!device_user)
        return;

    std::cout << "\nWARNING: " << device_path << " is already in use by '" << device_user->process_name << "' (PID "
              << device_user->pid << ").\nAuto terminating it before proceeding, please wait...\n"
              << std::endl;

    if (device_user->process_name == MEDIA_LIBRARY_SERVICE_PROCESS_NAME && stop_media_library_service() &&
        wait_for_process_exit(device_user->pid))
    {
        std::cout << "NOTE: '" << MEDIA_LIBRARY_SERVICE_PROCESS_NAME << "' was stopped so this process can own "
                  << device_path << ". It will start again on the next boot, or with '"
                  << MEDIA_LIBRARY_SERVICE_INIT_SCRIPT << " start'.\n"
                  << std::endl;
        WEBSERVER_LOG_INFO("{} stopped", MEDIA_LIBRARY_SERVICE_PROCESS_NAME);
        return;
    }

    WEBSERVER_LOG_INFO("Auto terminating conflicting app - Sending SIGTERM to '{}' (PID {})", device_user->process_name,
                       device_user->pid);
    kill(device_user->pid, SIGTERM);

    if (!wait_for_process_exit(device_user->pid))
    {
        WEBSERVER_LOG_INFO("Process '{}' (PID {}) did not exit after SIGTERM, sending SIGKILL",
                           device_user->process_name, device_user->pid);
        kill(device_user->pid, SIGKILL);
    }
}

int main(int argc, char *argv[])
{
    WEBSERVER_LOG_INFO("Starting webserver");

    std::string medialib_config_path = "";
    bool use_medialib_service = false;
    flags_init(argc, argv, medialib_config_path, use_medialib_service);

    // In multi-process mode media_library_service owns the sensor, so the conflicting-user
    // sweep would terminate the very service this process is about to connect to.
    if (!use_medialib_service)
    {
        terminate_conflicting_device_user(SENSOR_DEVICE_PATH);
    }

    // These reach the media library through this process's environment, so they only take
    // effect in single-process mode. For the service, set them in /etc/default/media-library-service.config.
    setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);
    setenv("MEDIALIB_FD_DUP", "1", 1);

    // Disable persistence so analytics config gets updated when switching pipelines.
    application_analytics_config_t::is_persistent = false;

    Architecture arch = get_hailo_architecture();

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

    auto pipeline_factory = std::make_unique<webserver::pipeline::PipelineFactory>(*resources, arch, pipeline_t::Basic,
                                                                                   use_medialib_service);

    signal_utils::SignalHandler signal_handler(/*exit_on_signal=*/false);
    signal_handler.register_threaded_signal_handler([&svr](int signal) {
        std::cout << "Received signal " << signal << ", stopping pipeline..." << std::endl;
        g_webserver_stopping = true;
        svr->stop();
    });

    pipeline_factory->get_current_pipeline()->start();

    WEBSERVER_LOG_INFO("Webserver started");
    svr->listen("0.0.0.0", 80);
    if (!g_webserver_stopping)
        return 0;

    // svr is deliberately left alive: the signal callback may still be inside svr->stop().
    pipeline_factory = nullptr;
    resources = nullptr;

    std::cout << "Pipeline stopped, exiting now" << std::endl;
    // Use _Exit(0) to terminate the process immediately without running static
    // destructors, flushing stdio buffers, or calling any atexit/quick_exit handlers.
    _Exit(0);
}
