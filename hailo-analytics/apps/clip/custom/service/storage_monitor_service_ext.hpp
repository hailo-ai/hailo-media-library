#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <filesystem>
#include <sys/statvfs.h>
#include <tl/expected.hpp>
#include "common_utils.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "service/storage_listener.hpp"

namespace fs = std::filesystem;

class StorageMonitorServiceExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension
{
  public:
    // Configuration structure
    struct Config
    {
        std::string mount_location;       // e.g., "/var/volatile/"
        std::string root_directory;       // Root directory path
        std::string database_directory;   // Database folder path
        std::string faissdb_directory;    // FaissDB folder path
        std::string thumbnail_directory;  // Thumbnail folder path
        std::string video_directory;      // Video folder path
        float low_disk_threshold_percent; // Low disk space threshold (0.0-100.0)
        uint32_t check_interval_seconds;  // Monitoring interval
    };

    // Error types
    enum class Error
    {
        INVALID_MOUNT_POINT,
        DIRECTORY_NOT_FOUND,
        PERMISSION_DENIED,
        ALREADY_RUNNING,
        NOT_CONFIGURED,
        FILESYSTEM_ERROR,
        INVALID_PARAMETER
    };

    // Constructor
    StorageMonitorServiceExt() = default;

    // Destructor
    ~StorageMonitorServiceExt()
    {
        stop();
    }

    // Delete copy constructor and assignment operator
    StorageMonitorServiceExt(const StorageMonitorServiceExt &) = delete;
    StorageMonitorServiceExt &operator=(const StorageMonitorServiceExt &) = delete;

    // Configure the service
    tl::expected<void, Error> configure(const Config &config)
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);

        if (m_is_running.load())
        {
            return tl::unexpected(Error::ALREADY_RUNNING);
        }

        Config processed_config = process_config_paths(config);

        // Validate mount point
        auto mount_validation = validate_mount_point(processed_config.mount_location);
        if (!mount_validation)
        {
            return tl::unexpected(mount_validation.error());
        }

        // Validate directories
        auto dir_validation = validate_directories(processed_config);
        if (!dir_validation)
        {
            return tl::unexpected(dir_validation.error());
        }

        // Validate threshold
        if (processed_config.low_disk_threshold_percent < 0.0f || processed_config.low_disk_threshold_percent > 100.0f)
        {
            return tl::unexpected(Error::INVALID_MOUNT_POINT);
        }

        m_config = processed_config;
        m_is_configured = true;
        return {};
    }

    // Configure with constructor-style initialization
    StorageMonitorServiceExt(const Config &config)
    {
        auto result = configure(config);
        if (!result)
        {
            // In a real implementation, you might want to handle this differently
            // since constructors can't return errors directly
            m_is_configured = false;
        }
    }

    // Add listener for storage events, check IStorageListener for details
    tl::expected<void, Error> add_listener(std::shared_ptr<IStorageListener> listener)
    {
        if (!listener)
        {
            return tl::unexpected(Error::INVALID_PARAMETER);
        }

        std::lock_guard<std::mutex> lock(m_listeners_mutex);
        m_listeners.push_back(listener);
        return {};
    }

    // Start monitoring
    tl::expected<void, Error> start()
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);

        if (!m_is_configured)
        {
            return tl::unexpected(Error::NOT_CONFIGURED);
        }

        if (m_is_running.load())
        {
            return tl::unexpected(Error::ALREADY_RUNNING);
        }

        m_should_stop.store(false);
        m_is_running.store(true);

        m_monitor_thread = std::thread(&StorageMonitorServiceExt::monitor_loop, this);
        return {};
    }

    // Stop monitoring
    void stop()
    {
        m_should_stop.store(true);
        if (m_monitor_thread.joinable())
        {
            m_monitor_thread.join();
        }
        m_is_running.store(false);
    }

    // Get current storage information (thread-safe)
    tl::expected<IStorageListener::StorageInfo, Error> get_storage_info() const
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);

        if (!m_is_configured)
        {
            return tl::unexpected(Error::NOT_CONFIGURED);
        }

        return m_current_storage_info;
    }

    // Check if service is running
    bool is_running() const
    {
        return m_is_running.load();
    }

    // Check if service is configured
    bool is_configured() const
    {
        return m_is_configured;
    }

    // Get database directory
    tl::expected<std::string, Error> get_sqldatabase_directory() const
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);

        if (!m_is_configured)
        {
            return tl::unexpected(Error::NOT_CONFIGURED);
        }

        return m_config.database_directory;
    }

    // Get video directory
    tl::expected<std::string, Error> get_video_directory() const
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);

        if (!m_is_configured)
        {
            return tl::unexpected(Error::NOT_CONFIGURED);
        }

        return m_config.video_directory;
    }

    // Get thumbnail directory
    tl::expected<std::string, Error> get_thumbnail_directory() const
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);

        if (!m_is_configured)
        {
            return tl::unexpected(Error::NOT_CONFIGURED);
        }

        return m_config.thumbnail_directory;
    }

    // Get FaissDB directory
    tl::expected<std::string, Error> get_faissdb_directory() const
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);

        if (!m_is_configured)
        {
            return tl::unexpected(Error::NOT_CONFIGURED);
        }

        return m_config.faissdb_directory;
    }

    static Config process_config_paths(const Config &input_config)
    {
        Config processed_config = input_config;

        // Join root_directory with mount_location
        processed_config.root_directory =
            FileSysUtils::join_path(input_config.mount_location, input_config.root_directory);

        // Join media directories with mount_location + root_directory
        std::string base_media_path = processed_config.root_directory;

        processed_config.database_directory = FileSysUtils::join_path(base_media_path, input_config.database_directory);
        processed_config.faissdb_directory = FileSysUtils::join_path(base_media_path, input_config.faissdb_directory);
        processed_config.thumbnail_directory =
            FileSysUtils::join_path(base_media_path, input_config.thumbnail_directory);
        processed_config.video_directory = FileSysUtils::join_path(base_media_path, input_config.video_directory);

        return processed_config;
    }

  private:
    // Configuration
    Config m_config;
    bool m_is_configured = false;
    mutable std::mutex m_config_mutex;

    // Threading
    std::thread m_monitor_thread;
    std::atomic<bool> m_is_running{false};
    std::atomic<bool> m_should_stop{false};

    // Data protection
    mutable std::mutex m_data_mutex;
    IStorageListener::StorageInfo m_current_storage_info{};

    // Concurrency control for the listeners list (weak pointers)
    std::mutex m_listeners_mutex;
    std::vector<std::weak_ptr<IStorageListener>> m_listeners;

    void notify_listeners(const IStorageListener::StorageInfo &info)
    {
        // We create a temporary list of STRONG pointers to ensure the listeners
        // stay alive while we are iterating over them.
        std::vector<std::shared_ptr<IStorageListener>> active_listeners;

        { // Scope for the lock
            std::lock_guard<std::mutex> lock(m_listeners_mutex);

            // Reserve space to avoid multiple reallocations
            active_listeners.reserve(m_listeners.size());

            // Iterate through the weak pointers and "lock" them.
            for (const auto &weak_listener : m_listeners)
            {
                // weak_ptr::lock() attempts to create a shared_ptr.
                // If the original object is still alive, it succeeds.
                // If the object has been destroyed, it returns a null shared_ptr.
                if (auto shared_listener = weak_listener.lock())
                {
                    // The object is alive! Add the strong pointer to our list.
                    active_listeners.push_back(shared_listener);
                }
            }

            // As an optional but good practice, we clean up the expired pointers.
            prune_expired_listeners();
        }

        // Now, with the lock released, notify the active listeners.
        // Because we hold shared_ptrs, we guarantee the listeners won't be
        // destroyed mid-notification.
        for (const auto &listener : active_listeners)
        {
            listener->on_storage_update_notification(info);
        }
    }

    // Validate mount point
    tl::expected<void, Error> validate_mount_point(const std::string &mount_path) const
    {
        try
        {
            if (!fs::exists(mount_path))
            {
                return tl::unexpected(Error::INVALID_MOUNT_POINT);
            }

            // Try to get filesystem stats to ensure it's a valid mount point
            struct statvfs stat;
            if (statvfs(mount_path.c_str(), &stat) != 0)
            {
                return tl::unexpected(Error::INVALID_MOUNT_POINT);
            }

            return {};
        }
        catch (const fs::filesystem_error &)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }
    }

    // Validate all directories
    tl::expected<void, Error> validate_directories(const Config &config) const
    {
        std::vector<std::string> directories = {config.root_directory, config.database_directory,
                                                config.faissdb_directory, config.thumbnail_directory,
                                                config.video_directory};

        try
        {
            for (const auto &dir : directories)
            {
                // We make sure the directory exists by creating it if it doesn't
                if (!FileSysUtils::ensure_directory_exists(dir))
                {
                    HAILO_ANALYTICS_LOG_ERROR("Failed to ensure directory exists: {}", dir);
                    return tl::unexpected(Error::DIRECTORY_NOT_FOUND); // Directory creation failed
                }
            }
            return {};
        }
        catch (const fs::filesystem_error &)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }
    }

    // Get mount point information
    tl::expected<void, Error> get_mount_info(IStorageListener::StorageInfo &info) const
    {
        struct statvfs stat;
        if (statvfs(m_config.mount_location.c_str(), &stat) != 0)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }

        info.mount_total_space = static_cast<uint64_t>(stat.f_blocks) * stat.f_frsize;
        info.mount_free_space = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;
        info.mount_used_space = info.mount_total_space - info.mount_free_space;

        info.disk_usage_percent =
            (static_cast<float>(info.mount_used_space) / static_cast<float>(info.mount_total_space)) * 100.0f;

        float free_percent =
            (static_cast<float>(info.mount_free_space) / static_cast<float>(info.mount_total_space)) * 100.0f;

        info.is_low_disk_space = (free_percent <= m_config.low_disk_threshold_percent);

        return {};
    }

    // Get directory size recursively
    tl::expected<uint64_t, Error> get_directory_size(const std::string &path) const
    {
        try
        {
            uint64_t size = 0;
            for (const auto &entry : fs::recursive_directory_iterator(path))
            {
                if (entry.is_regular_file())
                {
                    std::error_code ec;
                    auto file_size = entry.file_size(ec);
                    if (!ec)
                    {
                        size += file_size;
                    }
                }
            }
            return size;
        }
        catch (const fs::filesystem_error &)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }
    }

    // Get directory size using fast method
    // This uses the `du` command to get size quickly, but requires shell access
    tl::expected<uint64_t, Error> get_directory_size_fast(const std::string &path) const
    {
        // Escape the path to handle spaces and special characters
        std::string escaped_path;
        escaped_path.reserve(path.length() + 20);
        escaped_path += "'";
        for (char c : path)
        {
            if (c == '\'')
            {
                escaped_path += "'\"'\"'"; // Handle single quotes in path
            }
            else
            {
                escaped_path += c;
            }
        }
        escaped_path += "'";

        // Build the command: du -sb 'path' 2>/dev/null | cut -f1
        std::string command = "du -sb " + escaped_path + " 2>/dev/null | cut -f1";

        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }

        // Read the output
        char buffer[128];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            result += buffer;
        }

        int exit_code = pclose(pipe);

        // Check if command failed
        if (exit_code != 0)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }

        // Remove trailing whitespace/newline
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
        {
            result.pop_back();
        }

        // Check if we got empty result
        if (result.empty())
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }

        // Convert string to uint64_t
        try
        {
            // Validate that result contains only digits
            for (char c : result)
            {
                if (!std::isdigit(c))
                {
                    return tl::unexpected(Error::FILESYSTEM_ERROR);
                }
            }

            uint64_t size = std::stoull(result);
            return size;
        }
        catch (const std::invalid_argument &)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }
        catch (const std::out_of_range &)
        {
            return tl::unexpected(Error::FILESYSTEM_ERROR);
        }
    }

    // Update storage information
    tl::expected<void, Error> update_storage_info()
    {
        IStorageListener::StorageInfo new_info{};

        // Get mount point information
        auto mount_result = get_mount_info(new_info);
        if (!mount_result)
        {
            return tl::unexpected(mount_result.error());
        }

        // Get directory sizes
        auto root_size = get_directory_size_fast(m_config.root_directory);
        if (!root_size)
            return tl::unexpected(root_size.error());
        new_info.root_directory_size = *root_size;

        auto db_size = get_directory_size_fast(m_config.database_directory);
        if (!db_size)
            return tl::unexpected(db_size.error());
        new_info.database_directory_size = *db_size;

        auto faiss_size = get_directory_size_fast(m_config.faissdb_directory);
        if (!faiss_size)
            return tl::unexpected(faiss_size.error());
        new_info.faissdb_directory_size = *faiss_size;

        auto thumb_size = get_directory_size_fast(m_config.thumbnail_directory);
        if (!thumb_size)
            return tl::unexpected(thumb_size.error());
        new_info.thumbnail_directory_size = *thumb_size;

        auto video_size = get_directory_size_fast(m_config.video_directory);
        if (!video_size)
            return tl::unexpected(video_size.error());
        new_info.video_directory_size = *video_size;

        // Update current info (thread-safe)
        {
            std::lock_guard<std::mutex> lock(m_data_mutex);
            m_current_storage_info = new_info;
        }

        // Notify listeners
        notify_listeners(new_info);

        return {};
    }

    // Main monitoring loop
    void monitor_loop()
    {
        while (!m_should_stop.load())
        {
            auto result = update_storage_info();
            if (!result)
            {
                HAILO_ANALYTICS_LOG_ERROR("Error updating storage info: {}", static_cast<int>(result.error()));
            }

            // Sleep for the configured interval
            auto sleep_duration = std::chrono::seconds(m_config.check_interval_seconds);
            auto start_time = std::chrono::steady_clock::now();

            while (!m_should_stop.load() && (std::chrono::steady_clock::now() - start_time) < sleep_duration)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void prune_expired_listeners()
    {
        // This should be called inside a lock.
        m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(),
                                         [](const std::weak_ptr<IStorageListener> &p) { return p.expired(); }),
                          m_listeners.end());
    }
};

// Helper function to convert bytes to human-readable format
inline std::string format_bytes(uint64_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    int unit_index = 0;

    while (size >= 1024.0 && unit_index < 4)
    {
        size /= 1024.0;
        unit_index++;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit_index]);
    return std::string(buffer);
}
