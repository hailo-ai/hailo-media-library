#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <sys/statvfs.h>
#include <tl/expected.hpp>
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
    ~StorageMonitorServiceExt();

    // Delete copy constructor and assignment operator
    StorageMonitorServiceExt(const StorageMonitorServiceExt &) = delete;
    StorageMonitorServiceExt &operator=(const StorageMonitorServiceExt &) = delete;

    tl::expected<void, Error> configure(const Config &config);
    StorageMonitorServiceExt(const Config &config);
    tl::expected<void, Error> add_listener(std::shared_ptr<IStorageListener> listener);
    tl::expected<void, Error> start();
    void stop();
    tl::expected<IStorageListener::StorageInfo, Error> get_storage_info() const;

    bool is_running() const;

    bool is_configured() const;

    tl::expected<std::string, Error> get_sqldatabase_directory() const;
    tl::expected<std::string, Error> get_video_directory() const;
    tl::expected<std::string, Error> get_thumbnail_directory() const;
    tl::expected<std::string, Error> get_faissdb_directory() const;

    static Config process_config_paths(const Config &input_config);

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

    void notify_listeners(const IStorageListener::StorageInfo &info);
    tl::expected<void, Error> validate_mount_point(const std::string &mount_path) const;
    tl::expected<void, Error> validate_directories(const Config &config) const;
    tl::expected<void, Error> get_mount_info(IStorageListener::StorageInfo &info) const;
    tl::expected<uint64_t, Error> get_directory_size(const std::string &path) const;
    tl::expected<uint64_t, Error> get_directory_size_fast(const std::string &path) const;
    tl::expected<void, Error> update_storage_info();
    void monitor_loop();
    void prune_expired_listeners();
};

// Helper function to convert bytes to human-readable format
std::string format_bytes(uint64_t bytes);
