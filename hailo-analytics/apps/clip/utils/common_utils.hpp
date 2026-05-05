#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <vector>
#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

constexpr const char *VOLATILE_PATH = "/var/volatile";

namespace fs = std::filesystem;

namespace FileSysUtils
{

std::string read_file(const std::string &filename);

bool ensure_directory_exists(const std::string &path);

std::string join_path(const std::string &base, const std::string &relative);

std::string join_path_and_file_name(const std::string &base, const std::string &filename);

std::string extract_file_name(const std::string &full_path);

int move_file_sendfile(const std::string &src, const std::string &dst);

// Delete files
std::size_t delete_files(const std::vector<std::string> &file_paths);

// Returns all file names from a given directory path; if include_path is true, returns full paths
// If with_name_prefix is not empty, only returns files that start with the specified prefix
std::vector<std::string> get_all_file_names(const std::string &dir_path, bool include_path = false,
                                            const std::string &with_name_prefix = "");

} // namespace FileSysUtils

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace CodecUtils
{

enum class CodecType
{
    H264,
    H265,
    UNKNOWN
};

CodecType detect_codec_type(const std::string &filePath);

} // namespace CodecUtils

namespace SystemUtils
{

// Template class to handle any callback signature
template <typename... Args> class AsyncCallbackHandler
{
  public:
    // The type for the callback function
    using Callback = std::function<void(Args...)>;

    AsyncCallbackHandler() : m_stop(false)
    {
        // Start the worker thread in the constructor
        m_worker = std::thread(&AsyncCallbackHandler::workerLoop, this);
    }

    ~AsyncCallbackHandler()
    {
        // On destruction, signal the worker to stop
        m_stop = true;
        // Notify the condition variable to wake up the worker thread if it's waiting
        m_cv.notify_one();
        // Wait for the worker thread to finish its current task and exit
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    // Non-copyable and non-movable for simplicity
    AsyncCallbackHandler(const AsyncCallbackHandler &) = delete;
    AsyncCallbackHandler &operator=(const AsyncCallbackHandler &) = delete;

    // Register a new callback function for notifications
    void register_callback(Callback cb)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callbacks.push_back(cb);
    }

    // The main thread calls this function to trigger the callbacks with data.
    // This function is non-blocking.
    void trigger_callbacks(Args... args)
    {
        // Use a tuple to perfectly forward all arguments
        std::tuple<Args...> task_args(std::forward<Args>(args)...);

        {
            // Lock the queue to safely add a new task
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_taskQueue.push(task_args);
        } // Lock is released here

        // Notify the worker thread that there is a new task in the queue
        m_cv.notify_one();
    }

  private:
    // The main loop for the worker thread
    void workerLoop()
    {
        while (!m_stop)
        {
            std::tuple<Args...> task_args;
            {
                // Acquire lock on the queue
                std::unique_lock<std::mutex> lock(m_queueMutex);

                // Wait until the queue is not empty OR the stop flag is set
                // The lambda prevents spurious wakeups.
                m_cv.wait(lock, [this] { return !m_taskQueue.empty() || m_stop; });

                // If we woke up because of the stop flag and the queue is empty, exit
                if (m_stop && m_taskQueue.empty())
                {
                    return;
                }

                // Get the task from the front of the queue
                task_args = m_taskQueue.front();
                m_taskQueue.pop();

            } // The unique_lock is released here, before executing the callbacks

            // Execute all registered callbacks with the task data.
            // We do this outside the lock to avoid blocking the triggerCallbacks
            // function for a long time if a callback is slow.
            execute_callbacks(task_args);
        }
    }

    // Helper to call the functions using the tuple
    void execute_callbacks(const std::tuple<Args...> &args)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        for (const auto &cb : m_callbacks)
        {
            // std::apply invokes the callable 'cb' with arguments from the tuple 'args'
            std::apply(cb, args);
        }
    }

    // Member variables
    std::vector<Callback> m_callbacks;
    std::mutex m_callbackMutex; // Protects access to m_callbacks

    std::queue<std::tuple<Args...>> m_taskQueue;
    std::mutex m_queueMutex;      // Protects access to m_taskQueue
    std::condition_variable m_cv; // For signaling the worker

    std::thread m_worker;
    std::atomic<bool> m_stop;
};

std::shared_ptr<uint8_t> page_aligned_alloc(size_t size);
unsigned long long getTotalMemoryBytes();
double getTotalMemoryGB();

} // namespace SystemUtils
