#include "pipe_handler.hpp"
#include "media_library_logger.hpp"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <array>

#define MODULE_NAME LoggerType::NamedPipe

PipeHandler::PipeHandler(const std::string &pipe_path, CommandCallback callback, const std::string &response_pipe_path)
    : m_pipe_path(pipe_path), m_response_pipe_path(response_pipe_path), m_callback(callback), m_running(false)
{
}

PipeHandler::~PipeHandler()
{
    stop();

    if (std::filesystem::exists(m_pipe_path))
    {
        std::filesystem::remove(m_pipe_path);
    }

    if (!m_response_pipe_path.empty() && std::filesystem::exists(m_response_pipe_path))
    {
        std::filesystem::remove(m_response_pipe_path);
    }
}

bool PipeHandler::start()
{
    if (m_running)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Pipe handler already running");
        return false;
    }

    if (!create_named_pipe(m_pipe_path))
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create command pipe at {}", m_pipe_path);
        return false;
    }

    // Create response pipe if a path was provided
    if (!m_response_pipe_path.empty())
    {
        if (!create_named_pipe(m_response_pipe_path))
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create response pipe at {}", m_response_pipe_path);
            std::filesystem::remove(m_pipe_path);
            return false;
        }
        LOGGER__MODULE__INFO(MODULE_NAME, "Response pipe created at {}", m_response_pipe_path);
    }

    m_running = true;
    m_pipe_thread = std::thread([this]() { monitor_pipe(); });

    LOGGER__MODULE__INFO(MODULE_NAME, "Pipe handler started at {}", m_pipe_path);
    return true;
}

void PipeHandler::stop()
{
    m_running = false;
    if (m_pipe_thread.joinable())
    {
        m_pipe_thread.join();
    }
}

bool PipeHandler::is_running() const
{
    return m_running;
}

bool PipeHandler::create_named_pipe(const std::string &path)
{
    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path);
    }

    if (mkfifo(path.c_str(), 0666) != 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create named pipe at {}: {}", path, strerror(errno));
        return false;
    }

    return true;
}

int PipeHandler::open_pipe_fd(const std::string &pipe_path)
{
    int pipe_fd = open(pipe_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (pipe_fd == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open named pipe for reading: {}", strerror(errno));
    }
    return pipe_fd;
}

int PipeHandler::setup_epoll(int pipe_fd)
{
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to create epoll instance: {}", strerror(errno));
        return -1;
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = pipe_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pipe_fd, &event) == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to add pipe_fd to epoll: {}", strerror(errno));
        close(epoll_fd);
        return -1;
    }

    return epoll_fd;
}

int PipeHandler::handle_pipe_eof(int pipe_fd, int epoll_fd)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "EOF detected, reopening pipe");

    // Remove old fd from epoll
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pipe_fd, nullptr) == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to remove pipe_fd from epoll: {}", strerror(errno));
    }

    close(pipe_fd);

    int new_pipe_fd = open_pipe_fd(m_pipe_path);
    if (new_pipe_fd == -1)
    {
        return -1;
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = new_pipe_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_pipe_fd, &event) == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to re-add pipe_fd to epoll: {}", strerror(errno));
        close(new_pipe_fd);
        return -1;
    }

    return new_pipe_fd;
}

void PipeHandler::process_pipe_events(int pipe_fd, int epoll_fd)
{
    struct epoll_event events[1];

    while (m_running)
    {
        int num_events = epoll_wait(epoll_fd, events, 1, 500); // 500ms timeout

        if (num_events > 0)
        {
            bool eof_detected = !handle_pipe_read(pipe_fd);
            if (eof_detected)
            {
                pipe_fd = handle_pipe_eof(pipe_fd, epoll_fd);
                if (pipe_fd == -1)
                {
                    break;
                }
            }
        }
        else if (num_events == -1 && errno != EINTR)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Error in epoll_wait: {}", strerror(errno));
            break;
        }
    }
}

void PipeHandler::monitor_pipe()
{
    int pipe_fd = open_pipe_fd(m_pipe_path);
    if (pipe_fd == -1)
    {
        m_running = false;
        return;
    }

    int epoll_fd = setup_epoll(pipe_fd);
    if (epoll_fd == -1)
    {
        close(pipe_fd);
        m_running = false;
        return;
    }

    process_pipe_events(pipe_fd, epoll_fd);

    close(epoll_fd);
    close(pipe_fd);
    LOGGER__MODULE__INFO(MODULE_NAME, "Pipe handler stopped");
}

bool PipeHandler::handle_pipe_read(int pipe_fd)
{
    std::array<char, 128> buffer{};
    ssize_t bytes_read = read(pipe_fd, buffer.data(), buffer.size() - 1);

    if (bytes_read < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            LOGGER__MODULE__ERROR(MODULE_NAME, "Error reading from pipe: {}", strerror(errno));
        return true; // Not EOF, just an error
    }

    if (bytes_read == 0)
    {
        // EOF detected
        return false;
    }

    // Data was read successfully
    buffer[bytes_read] = '\0'; // Null-terminate the string
    std::string command(buffer.data());
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Received command: '{}'", trim(command));

    if (m_callback)
    {
        std::string response = m_callback(trim(command));

        // Only try to write response if response pipe is configured and response isn't empty
        if (!m_response_pipe_path.empty() && !response.empty())
        {
            if (!write_response(response))
            {
                // Only log as debug instead of error, since it's expected that sometimes
                // no process is reading from the response pipe
                LOGGER__MODULE__DEBUG(MODULE_NAME, "Could not write response - likely no reader on response pipe");
            }
            else
            {
                LOGGER__MODULE__DEBUG(MODULE_NAME, "Response sent: '{}'", response);
            }
        }
    }

    return true; // Not EOF
}

bool PipeHandler::write_response(const std::string &response)
{
    if (m_response_pipe_path.empty())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Response pipe path is not set");
        return false;
    }

    // Open response pipe for writing
    int write_fd = open(m_response_pipe_path.c_str(), O_WRONLY | O_NONBLOCK);
    if (write_fd == -1)
    {
        // This is expected if no process has the pipe open for reading
        if (errno != ENXIO)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open response pipe for writing: {}", strerror(errno));
        }
        return false;
    }

    // Write the response
    ssize_t bytes_written = write(write_fd, response.c_str(), response.size());
    close(write_fd);

    if (bytes_written == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write to response pipe: {}", strerror(errno));
        return false;
    }

    return true;
}

std::string PipeHandler::trim(const std::string &str)
{
    const auto begin = str.find_first_not_of(" \t\n\r");
    const auto end = str.find_last_not_of(" \t\n\r");
    return (begin == std::string::npos || end == std::string::npos) ? "" : str.substr(begin, end - begin + 1);
}
