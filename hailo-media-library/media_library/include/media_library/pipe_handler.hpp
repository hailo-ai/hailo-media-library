#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

/**
 * @brief Generic handler for Linux named pipes
 *
 * This class provides functionality to create and monitor a named pipe,
 * and process commands received through it.
 */
class PipeHandler
{
  public:
    using CommandCallback = std::function<std::string(const std::string &)>;

    /**
     * @brief Construct a new Pipe Handler object
     *
     * @param pipe_path The path where the command named pipe will be created
     * @param callback Function to call when a command is received
     * @param response_pipe_path Optional path for a response pipe. If provided, responses will be written here.
     */
    PipeHandler(const std::string &pipe_path, CommandCallback callback, const std::string &response_pipe_path = "");

    /**
     * @brief Destroy the Pipe Handler object
     * Stops the monitoring thread and cleans up resources
     */
    ~PipeHandler();

    /**
     * @brief Start monitoring the pipe in a separate thread
     *
     * @return true if pipe was successfully created and monitoring started
     * @return false if there was an error
     */
    bool start();

    /**
     * @brief Stop monitoring the pipe
     */
    void stop();

    /**
     * @brief Check if the pipe handler is running
     *
     * @return true if running
     * @return false otherwise
     */
    bool is_running() const;

  private:
    std::string m_pipe_path;
    std::string m_response_pipe_path;
    CommandCallback m_callback;
    std::atomic<bool> m_running;
    std::thread m_pipe_thread;

    bool create_named_pipe(const std::string &path);
    void monitor_pipe();
    bool handle_pipe_read(int pipe_fd);
    bool write_response(const std::string &response);
    std::string trim(const std::string &str);

    int open_pipe_fd(const std::string &pipe_path);
    int setup_epoll(int pipe_fd);
    void process_pipe_events(int pipe_fd, int epoll_fd);
    int handle_pipe_eof(int pipe_fd, int epoll_fd);
};
