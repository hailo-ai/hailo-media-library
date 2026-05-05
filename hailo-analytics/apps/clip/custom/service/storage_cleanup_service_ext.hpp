#pragma once

#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <tl/expected.hpp>

#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"

#include "service/storage_listener.hpp"

// Forward declarations
class FaissTable;
class ThumbnailTable;
class VideoTable;
class ICleanupStrategy;

class StorageCleanupServiceExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension,
                                 public IStorageListener
{
  public:
    struct DatabaseConfig
    {
        std::string faiss_db_name;
        std::string thumbnail_db_name;
        std::string video_db_name;

        DatabaseConfig(const std::string &faiss, const std::string &thumbnail, const std::string &video);
    };

    StorageCleanupServiceExt();
    ~StorageCleanupServiceExt();

    void on_storage_update_notification(const StorageInfo &info) override;

    // Initialize modules that might fail
    tl::expected<void, std::string> initialize(std::unique_ptr<ICleanupStrategy> strategy,
                                               const DatabaseConfig &db_config);

    // Send cleanup request to the service
    void request_cleanup(StorageInfo request);

    void stop();

    // Getters
    std::shared_ptr<FaissTable> get_faiss_table() const;
    std::shared_ptr<ThumbnailTable> get_thumbnail_table() const;
    std::shared_ptr<VideoTable> get_video_table() const;

  private:
    void worker_loop();
    void process_cleanup_request(const StorageInfo &info);

  private:
    // Add member variables for initialized resources
    std::shared_ptr<FaissTable> m_faiss_table;
    std::shared_ptr<ThumbnailTable> m_thumbnail_table;
    std::shared_ptr<VideoTable> m_video_table;

    std::unique_ptr<ICleanupStrategy> m_strategy;

    std::queue<StorageInfo> m_request_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::thread m_worker_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_cleanup_in_progress{false};
    std::mutex m_init_mutex;
};
