#include <chrono>
#include <thread>
#include <signal.h>
#include <nlohmann/json.hpp>
#include <cxxopts/cxxopts.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
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

static std::atomic<bool> g_webserver_stopping{false};

bool is_device_in_use(const std::string &device_path)
{
    struct stat dev_stat;
    if (stat(device_path.c_str(), &dev_stat) != 0)
        return false;

    pid_t self = getpid();
    std::error_code ec;

    for (const auto &proc : std::filesystem::directory_iterator("/proc", ec))
    {
        const auto &path = proc.path();
        const auto name = path.filename().string();

        if (name.empty() || !std::isdigit(name[0]))
            continue;

        if (std::stoi(name) == self)
            continue;

        std::string comm;
        if (std::ifstream(path / "comm") >> comm && comm.rfind("isp_med", 0) == 0)
            continue;

        for (const auto &fd : std::filesystem::directory_iterator(path / "fd", ec))
        {
            struct stat fd_stat;
            if (stat(fd.path().c_str(), &fd_stat) == 0 && S_ISCHR(fd_stat.st_mode) &&
                fd_stat.st_rdev == dev_stat.st_rdev)
                return true;
        }
    }
    return false;
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

    if (is_device_in_use("/dev/video0"))
    {
        WEBSERVER_LOG_ERROR("/dev/video0 is already in use by another process");
        return 1;
    }

    setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);
    setenv("MEDIALIB_FD_DUP", "1", 1);

    std::string medialib_config_path = "";
    Architecture arch = get_hailo_architecture();
    flags_init(argc, argv, medialib_config_path);

    std::shared_ptr<HTTPServer> svr = HTTPServer::create();
    // register error handler
    svr->set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
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
    WebserverResourceRepository resources = webserver::resources::ResourceRepository::create(svr, medialib_config_path);

    auto pipeline_factory = std::make_shared<webserver::pipeline::PipelineFactory>(resources, arch, pipeline_t::Basic);

    signal_utils::SignalHandler signal_handler;
    signal_handler.register_signal_handler([pipeline_factory](int signal) {
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

        auto pipeline = pipeline_factory->get_current_pipeline();
        if (pipeline)
        {
            pipeline->stop();
        }
        std::cout << "Pipeline stopped, exiting now" << std::endl;
        // Use _Exit(0) to terminate the process immediately without running static
        // destructors, flushing stdio buffers, or calling any atexit/quick_exit handlers.
        _Exit(0);
    });

    pipeline_factory->get_current_pipeline()->start();

    WEBSERVER_LOG_INFO("Webserver started");
    svr->listen("0.0.0.0", 80);
}
