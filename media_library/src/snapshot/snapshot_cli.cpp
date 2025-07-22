#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logger_macros.hpp"
#include "media_library_logger.hpp"
#include "snapshot.hpp"

#define MODULE_NAME LoggerType::Snapshot

// Global signal handlers
void signal_handler_callback(int signal);
void sigusr1_handler_callback(int);

class SnapshotCli
{
  private:
    static constexpr int RESPONSE_TIMEOUT_MS = 500;
    static constexpr int COMMAND_TIMEOUT_SEC = 10;
    static constexpr int PIPE_POLL_INTERVAL_MS = 50;
    static constexpr int MONITOR_SLEEP_INTERVAL_MS = 100;
    static constexpr size_t READ_BUFFER_SIZE = 256;

    // Control flags
    std::atomic<bool> m_running;

    // Pipe communication
    std::mutex m_response_mutex;
    std::condition_variable m_response_cv;
    std::string m_pending_command;
    std::string m_received_response;
    bool m_waiting_for_response;

    // Threading
    std::thread m_monitor_thread;

    // Command handlers
    using CommandHandler = std::function<void(SnapshotCli *, const std::vector<std::string> &)>;
    std::map<std::string, CommandHandler> m_command_handlers;

  public:
    SnapshotCli() : m_running(false), m_waiting_for_response(false)
    {
    }
    ~SnapshotCli()
    {
        cleanup();
    }

    void shutdown()
    {
        m_running = false;
    }

    bool initialize()
    {
        m_running = true;
        m_waiting_for_response = false;

        register_command_handlers();

        if (!check_pipes_exist())
        {
            return false;
        }

        m_monitor_thread = std::thread(&SnapshotCli::monitor_response_pipe, this);
        return true;
    }

    int run()
    {
        std::string input;
        while (m_running)
        {
            std::cout << "# ";
            std::cout.flush();

            if (!std::cin.good())
            {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }

            if (std::getline(std::cin, input).fail())
            {
                // If getline fails (e.g., due to Ctrl+C), check if we should exit
                if (!m_running)
                {
                    break;
                }
                continue;
            }

            if (!input.empty())
            {
                handle_user_input(input);
            }
        }

        cleanup();
        LOGGER__MODULE__INFO(MODULE_NAME, "Snapshot tool exiting.");
        return 0;
    }

  private:
    void register_command_handlers()
    {
        m_command_handlers["help"] = &SnapshotCli::handle_help_command;
        m_command_handlers["exit"] = &SnapshotCli::handle_exit_command;
        m_command_handlers["quit"] = &SnapshotCli::handle_exit_command;
        m_command_handlers["snapshot"] = &SnapshotCli::handle_snapshot_command;
        m_command_handlers["list_stages"] = &SnapshotCli::handle_list_stages_command;
    }

    bool check_pipes_exist()
    {
        std::filesystem::path pipe_path(SnapshotManager::PIPE_PATH);

        if (!std::filesystem::exists(pipe_path))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Snapshot feature is not enabled in Media Library.");
            LOGGER__MODULE__ERROR(MODULE_NAME, "Please export MEDIALIB_SNAPSHOT_ENABLE=1 environment variable.");
            return false;
        }
        return true;
    }

    void cleanup()
    {
        m_running = false;

        if (m_monitor_thread.joinable())
        {
            m_monitor_thread.join();
        }
    }

    void monitor_response_pipe()
    {
        while (m_running)
        {
            std::string response = read_from_response_pipe(SnapshotManager::RESPONSE_PIPE_PATH, RESPONSE_TIMEOUT_MS);
            if (!response.empty())
            {
                process_pipe_response(response);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(MONITOR_SLEEP_INTERVAL_MS));
        }
    }

    void process_pipe_response(const std::string &response)
    {
        std::lock_guard<std::mutex> lock(m_response_mutex);
        if (m_waiting_for_response)
        {
            m_received_response = response;
            m_waiting_for_response = false;
            m_response_cv.notify_one();
        }
        else
        {
            // This is an asynchronous response (e.g., from another process)
            LOGGER__MODULE__INFO(MODULE_NAME, "Async response: {}", response);
            std::cout << std::endl << response << std::endl << "# ";
            std::cout.flush();
        }
    }

    std::string read_from_response_pipe(const std::string &pipe_path, int timeout_ms)
    {
        int fd = try_open_pipe(pipe_path, timeout_ms);
        if (fd == -1)
        {
            return "";
        }

        std::string result = read_data_with_epoll(fd, timeout_ms);
        close(fd);
        return result;
    }

    int try_open_pipe(const std::string &pipe_path, int timeout_ms)
    {
        auto start_time = std::chrono::steady_clock::now();
        int fd = -1;

        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                   .count() < timeout_ms)
        {
            fd = open(pipe_path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd != -1)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(PIPE_POLL_INTERVAL_MS));
        }

        if (fd == -1)
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Failed to open response pipe: {}", pipe_path);
        }
        return fd;
    }

    std::string read_data_with_epoll(int fd, int timeout_ms)
    {
        std::string result;
        char buffer[READ_BUFFER_SIZE] = {0};

        int epoll_fd = epoll_create1(0);
        if (epoll_fd == -1)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create epoll instance");
            return result;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to add fd to epoll");
            close(epoll_fd);
            return result;
        }

        struct epoll_event events[1];
        int nfds = epoll_wait(epoll_fd, events, 1, timeout_ms);

        if (nfds > 0)
        {
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                result = buffer;
            }
        }

        close(epoll_fd);
        return result;
    }

    bool send_command_and_wait_response(const std::string &command)
    {
        prepare_for_response(command);

        if (!write_command_to_pipe(command))
        {
            return false;
        }

        return wait_for_response();
    }

    void prepare_for_response(const std::string &command)
    {
        std::lock_guard<std::mutex> lock(m_response_mutex);
        m_pending_command = command;
        m_received_response = "";
        m_waiting_for_response = true;
    }

    bool write_command_to_pipe(const std::string &command)
    {
        int fd = open(SnapshotManager::PIPE_PATH, O_WRONLY);
        if (fd == -1)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open command pipe");
            std::cout << "Error: Failed to open command pipe" << std::endl;
            return false;
        }

        ssize_t bytes_written = write(fd, command.c_str(), command.size());
        close(fd);

        if (bytes_written != static_cast<ssize_t>(command.size()))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write command to pipe");
            std::cout << "Error: Failed to write command to pipe" << std::endl;
            return false;
        }

        LOGGER__MODULE__DEBUG(MODULE_NAME, "Sent command: {}", command);
        return true;
    }

    bool wait_for_response()
    {
        std::unique_lock<std::mutex> lock(m_response_mutex);
        auto predicate = [this] { return !m_waiting_for_response; };

        if (m_response_cv.wait_for(lock, std::chrono::seconds(COMMAND_TIMEOUT_SEC), predicate))
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "Response received: {}", m_received_response);
            std::cout << m_received_response << std::endl;
            return true;
        }
        else
        {
            m_waiting_for_response = false;
            LOGGER__MODULE__WARNING(MODULE_NAME, "No response received (timeout)");
            std::cout << "No response received (timeout)" << std::endl;
            return false;
        }
    }

    std::vector<std::string> parse_command(const std::string &input)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(input);
        std::string token;

        while (ss >> token)
        {
            tokens.push_back(token);
        }

        return tokens;
    }

    void handle_user_input(const std::string &input)
    {
        std::vector<std::string> tokens = parse_command(input);

        if (tokens.empty())
        {
            return;
        }

        std::string command = tokens[0];
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        auto it = m_command_handlers.find(command);
        if (it != m_command_handlers.end())
        {
            (it->second)(this, args);
        }
        else
        {
            send_command_and_wait_response(input);
        }
    }
    void print_usage()
    {
        std::cout << R"(Hailo Media Library Snapshot Tool
---------------------------------

Commands:
    snapshot [frames_count] [stage1,stage2,...]
        Request a new snapshot
        - frames_count: Optional number of frames to capture (default: 1)
        - stage list:   Optional comma-separated list of stages to capture

    list_stages
        Show all available pipeline stages for snapshot

    help
        Show this help message

    exit
        Exit this tool

Examples:
    snapshot
        Capture 1 frame from all stages

    snapshot 5
        Capture 5 frames from all stages

    snapshot 3 post_isp,dewarp
        Capture 3 frames from 'post_isp' and 'dewarp' stages only

    list_stages
        List all available pipeline stages
)" << std::endl;
    }

    void handle_help_command(const std::vector<std::string> &)
    {
        print_usage();
    }
    void handle_exit_command(const std::vector<std::string> &)
    {
        shutdown();
    }
    void handle_snapshot_command(const std::vector<std::string> &args)
    {
        std::string command = "snapshot";
        for (const auto &arg : args)
        {
            command += " " + arg;
        }
        send_command_and_wait_response(command);
    }
    void handle_list_stages_command(const std::vector<std::string> &)
    {
        send_command_and_wait_response("list_stages");
    }
};

SnapshotCli *g_cli_instance = nullptr;

void signal_handler_callback(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        std::cout << std::endl << "Received interrupt signal. Exiting..." << std::endl;

        if (g_cli_instance)
        {
            g_cli_instance->shutdown();
        }

        // Wake up any blocked getline calls
        raise(SIGUSR1);
    }
}

void sigusr1_handler_callback(int)
{
    // Do nothing, just to ensure SIGUSR1 doesn't terminate the program
}

void setup_signal_handlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler_callback;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    struct sigaction sa_usr1;
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_handler = &sigusr1_handler_callback;
    sigaction(SIGUSR1, &sa_usr1, nullptr);
}

int main(int /*argc*/, char * /*argv*/[])
{
    setup_signal_handlers();

    SnapshotCli cli;
    g_cli_instance = &cli;

    int result = 1;
    if (cli.initialize())
    {
        result = cli.run();
    }

    g_cli_instance = nullptr;
    return result;
}
