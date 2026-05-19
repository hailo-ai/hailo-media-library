#include <fcntl.h>
#include <stdio.h> // IWYU pragma: keep
#include <sys/epoll.h>
#include <unistd.h>
#include <readline/readline.h>
#include <stdint.h>
#include <string.h>
#include <chrono>
#include <iostream>
#include <cerrno>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "snapshot_cli.hpp"
#include "media_library_logger.hpp"
#include "snapshot.hpp"

#define MODULE_NAME LoggerType::Snapshot

static constexpr const char *PROGRESS_PREFIX = "SNAPSHOT_PROGRESS:";
static constexpr const char *COMPLETE_PREFIX = "SNAPSHOT_COMPLETE:";
static constexpr size_t PROGRESS_PREFIX_LEN = 18;
static constexpr size_t COMPLETE_PREFIX_LEN = 18;

void SnapshotCli::monitor_response_pipe()
{
    // Open with O_RDWR so the pipe never sees EOF when the writer closes.
    // This keeps the fd permanently readable, avoiding the gap between
    // close/reopen where write_response() would get ENXIO.
    m_response_pipe_fd = open(SnapshotManager::RESPONSE_PIPE_PATH, O_RDWR | O_NONBLOCK);
    if (m_response_pipe_fd == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open response pipe: {}", strerror(errno));
        return;
    }

    m_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epoll_fd == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create epoll instance");
        close(m_response_pipe_fd);
        m_response_pipe_fd = -1;
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = m_response_pipe_fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_response_pipe_fd, &ev) == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to add fd to epoll");
        close(m_epoll_fd);
        close(m_response_pipe_fd);
        m_epoll_fd = -1;
        m_response_pipe_fd = -1;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_monitor_ready_mutex);
        m_monitor_ready = true;
    }
    m_monitor_ready_cv.notify_one();

    char buffer[READ_BUFFER_SIZE];
    struct epoll_event events[1];

    while (m_running)
    {
        int nfds = epoll_wait(m_epoll_fd, events, 1, RESPONSE_TIMEOUT_MS);
        if (nfds > 0)
        {
            ssize_t bytes_read = read(m_response_pipe_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                process_pipe_response(std::string(buffer, bytes_read));
            }
        }
    }

    close(m_epoll_fd);
    m_epoll_fd = -1;
    close(m_response_pipe_fd);
    m_response_pipe_fd = -1;
}

void SnapshotCli::process_pipe_response(const std::string &response)
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
        LOGGER__MODULE__INFO(MODULE_NAME, "Async response: {}", response);
        bool is_progress = response.compare(0, PROGRESS_PREFIX_LEN, PROGRESS_PREFIX) == 0;

        if (is_progress)
        {
            std::cout << format_response(response) << std::flush;
            return;
        }

        bool is_complete = response.compare(0, COMPLETE_PREFIX_LEN, COMPLETE_PREFIX) == 0;
        if (!is_complete)
        {
            std::cout << std::endl;
        }
        std::cout << format_response(response) << std::endl;
        if (isatty(STDIN_FILENO))
        {
            rl_on_new_line();
            rl_redisplay();
        }
    }
}

bool SnapshotCli::send_command_and_wait_response(const std::string &command, int timeout_sec)
{
    prepare_for_response(command);

    if (!write_command_to_pipe(command))
    {
        return false;
    }

    return wait_for_response(timeout_sec);
}

std::string SnapshotCli::send_command_silent(const std::string &command, int timeout_sec)
{
    prepare_for_response(command);

    if (!write_command_to_pipe(command))
    {
        return "";
    }

    std::unique_lock<std::mutex> lock(m_response_mutex);
    auto predicate = [this] { return !m_waiting_for_response || !m_running.load(); };

    if (m_response_cv.wait_for(lock, std::chrono::seconds(timeout_sec), predicate))
    {
        if (!m_running.load())
        {
            m_waiting_for_response = false;
            return "";
        }
        return m_received_response;
    }
    m_waiting_for_response = false;
    return "";
}

void SnapshotCli::prepare_for_response(const std::string &command)
{
    std::lock_guard<std::mutex> lock(m_response_mutex);
    m_pending_command = command;
    m_received_response = "";
    m_waiting_for_response = true;
}

bool SnapshotCli::write_command_to_pipe(const std::string &command)
{
    int fd = open(SnapshotManager::PIPE_PATH, O_WRONLY);
    if (fd == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open command pipe");
        print_error("Failed to open command pipe");
        return false;
    }

    ssize_t bytes_written = write(fd, command.c_str(), command.size());
    close(fd);

    if (bytes_written != static_cast<ssize_t>(command.size()))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write command to pipe");
        print_error("Failed to write command to pipe");
        return false;
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Sent command: {}", command);
    return true;
}

bool SnapshotCli::wait_for_response(int timeout_sec)
{
    std::unique_lock<std::mutex> lock(m_response_mutex);
    auto predicate = [this] { return !m_waiting_for_response || !m_running.load(); };

    if (m_response_cv.wait_for(lock, std::chrono::seconds(timeout_sec), predicate))
    {
        if (!m_running.load())
        {
            m_waiting_for_response = false;
            return false;
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "Response received: {}", m_received_response);
        std::cout << format_response(m_received_response) << std::endl;
        return true;
    }
    else
    {
        m_waiting_for_response = false;
        LOGGER__MODULE__WARNING(MODULE_NAME, "No response received (timeout after {}s)", timeout_sec);
        std::cout << color(ansi::BOLD_YELLOW) << "Warning:" << color(ansi::RESET)
                  << " No response received (timeout after " << timeout_sec << "s)" << std::endl;
        return false;
    }
}

void SnapshotCli::fetch_stages_for_completion()
{
    static constexpr int STAGE_FETCH_TIMEOUT_SEC = 3;
    std::string response = send_command_silent("list_stages", STAGE_FETCH_TIMEOUT_SEC);
    if (!response.empty())
    {
        parse_stages_from_response(response);
    }
}

void SnapshotCli::parse_stages_from_response(const std::string &response)
{
    g_cached_stages.clear();
    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.size() > 2 && line[0] == '-' && line[1] == ' ')
        {
            g_cached_stages.push_back(line.substr(2));
        }
    }
}

static std::string format_progress_bar(uint32_t current, uint32_t total, bool use_color)
{
    static constexpr int BAR_WIDTH = 20;
    int filled = (total > 0) ? static_cast<int>((static_cast<float>(current) / total) * BAR_WIDTH) : 0;
    int empty = BAR_WIDTH - filled;

    std::ostringstream bar;
    bar << "\r";
    if (use_color)
        bar << ansi::CYAN;
    bar << "Capturing snapshots [";
    for (int i = 0; i < filled; i++)
        bar << unicode::FULL_BLOCK;
    for (int i = 0; i < empty; i++)
        bar << unicode::LIGHT_SHADE;
    bar << "] " << current << "/" << total;
    if (use_color)
        bar << ansi::RESET;
    return bar.str();
}

static std::string format_complete_bar(uint32_t total, bool use_color)
{
    std::ostringstream out;
    out << "\r";
    if (use_color)
        out << ansi::BOLD_GREEN;
    out << "Captured " << total << "/" << total << " snapshots " << unicode::CHECK_MARK;
    if (use_color)
        out << ansi::RESET;
    // Pad with spaces to overwrite any leftover characters from the progress bar
    out << "                    ";
    return out.str();
}

std::string SnapshotCli::format_response(const std::string &response) const
{
    if (response.compare(0, 6, "Error:") == 0)
    {
        return color(ansi::BOLD_RED) + "Error:" + color(ansi::RESET) + response.substr(6);
    }

    if (response.compare(0, PROGRESS_PREFIX_LEN, PROGRESS_PREFIX) == 0)
    {
        std::string fraction = response.substr(PROGRESS_PREFIX_LEN);
        auto slash_pos = fraction.find('/');
        if (slash_pos != std::string::npos)
        {
            uint32_t current = std::stoul(fraction.substr(0, slash_pos));
            uint32_t total = std::stoul(fraction.substr(slash_pos + 1));
            return format_progress_bar(current, total, m_use_color);
        }
        return response;
    }

    if (response.compare(0, COMPLETE_PREFIX_LEN, COMPLETE_PREFIX) == 0)
    {
        uint32_t total = std::stoul(response.substr(COMPLETE_PREFIX_LEN));
        return format_complete_bar(total, m_use_color);
    }

    if (response.find("Snapshot requested") != std::string::npos)
    {
        return color(ansi::GREEN) + response + color(ansi::RESET);
    }

    if (response.find("Available stages") != std::string::npos)
    {
        return format_stages_response(response);
    }

    return response;
}

std::string SnapshotCli::format_stages_response(const std::string &response) const
{
    std::istringstream stream(response);
    std::string line;
    std::ostringstream formatted;

    while (std::getline(stream, line))
    {
        if (line.find("Available stages") != std::string::npos)
        {
            formatted << color(ansi::BOLD) << line << color(ansi::RESET) << "\n";
        }
        else if (line.size() > 2 && line[0] == '-' && line[1] == ' ')
        {
            formatted << "  " << color(ansi::CYAN) << line.substr(2) << color(ansi::RESET) << "\n";
        }
        else if (!line.empty())
        {
            formatted << color(ansi::DIM) << line << color(ansi::RESET) << "\n";
        }
    }

    std::string result = formatted.str();
    if (!result.empty() && result.back() == '\n')
    {
        result.pop_back();
    }
    return result;
}
