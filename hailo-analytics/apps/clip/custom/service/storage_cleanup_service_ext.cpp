#include "storage_cleanup_service_ext.hpp"

#include <iostream>

StorageCleanupServiceExt::DatabaseConfig::DatabaseConfig(const std::string &faiss, const std::string &thumbnail,
                                                         const std::string &video)
    : faiss_db_name(faiss), thumbnail_db_name(thumbnail), video_db_name(video)
{
}
#include "include/faiss_table.hpp"
#include "include/thumbnail_table.hpp"
#include "include/video_table.hpp"
#include "sql_factory.hpp"
#include "storage_cleanup_strategy.hpp"

StorageCleanupServiceExt::StorageCleanupServiceExt() : m_running(false), m_initialized(false)
{
    // Only start basic components, no complex initialization
}

StorageCleanupServiceExt::~StorageCleanupServiceExt()
{
    stop();
}

void StorageCleanupServiceExt::on_storage_update_notification(const StorageInfo &info)
{

    // This method is called by StorageMonitorServiceExt to notify about storage updates

    if (info.is_low_disk_space && m_request_queue.empty() && !m_cleanup_in_progress)
    {
        HAILO_ANALYTICS_LOG_INFO("Low disk space detected, triggering cleanup");
        // Trigger cleanup
        request_cleanup(info);
    }
}

tl::expected<void, std::string> StorageCleanupServiceExt::initialize(std::unique_ptr<ICleanupStrategy> strategy,
                                                                     const DatabaseConfig &db_config)
{
    std::lock_guard<std::mutex> lock(m_init_mutex);

    if (m_initialized)
    {
        return {}; // Already initialized
    }

    if (!strategy)
    {
        return tl::make_unexpected("Invalid cleanup strategy provided");
    }

    m_strategy = std::move(strategy);

    // Initialize database connections using the provided configuration
    auto faiss_table_result = SqlDatabaseQuickAccess::get_database(db_config.faiss_db_name);
    if (!faiss_table_result)
    {
        return tl::unexpected("Failed to initialize FaissTable with name '" + db_config.faiss_db_name +
                              "': " + faiss_table_result.error().message);
    }
    m_faiss_table = std::dynamic_pointer_cast<FaissTable>(faiss_table_result.value());

    auto thumbnail_table_result = SqlDatabaseQuickAccess::get_database(db_config.thumbnail_db_name);
    if (!thumbnail_table_result)
    {
        return tl::unexpected("Failed to initialize ThumbnailTable with name '" + db_config.thumbnail_db_name +
                              "': " + thumbnail_table_result.error().message);
    }
    m_thumbnail_table = std::dynamic_pointer_cast<ThumbnailTable>(thumbnail_table_result.value());

    auto video_table_result = SqlDatabaseQuickAccess::get_database(db_config.video_db_name);
    if (!video_table_result)
    {
        return tl::unexpected("VideoTable not found in SqlDatabaseQuickAccess with name '" + db_config.video_db_name +
                              "': " + video_table_result.error().message);
    }
    m_video_table = std::dynamic_pointer_cast<VideoTable>(video_table_result.value());

    m_initialized = true;
    m_running = true;

    // Start worker thread only after successful initialization
    m_worker_thread = std::thread(&StorageCleanupServiceExt::worker_loop, this);

    return {}; // Success
}

void StorageCleanupServiceExt::request_cleanup(StorageInfo request)
{
    if (!m_initialized)
    {
        HAILO_ANALYTICS_LOG_ERROR("Service not initialized, ignoring cleanup request");
        return;
    }

    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_request_queue.push(request);
    m_queue_cv.notify_one();
}

void StorageCleanupServiceExt::stop()
{
    m_running = false;
    m_queue_cv.notify_all();
    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }
}

std::shared_ptr<FaissTable> StorageCleanupServiceExt::get_faiss_table() const
{
    return m_faiss_table;
}

std::shared_ptr<ThumbnailTable> StorageCleanupServiceExt::get_thumbnail_table() const
{
    return m_thumbnail_table;
}

std::shared_ptr<VideoTable> StorageCleanupServiceExt::get_video_table() const
{
    return m_video_table;
}

void StorageCleanupServiceExt::worker_loop()
{
    while (m_running)
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_queue_cv.wait(lock, [this] { return !m_request_queue.empty() || !m_running; });

        while (!m_request_queue.empty() && m_running)
        {
            StorageInfo info = m_request_queue.front();
            m_request_queue.pop();
            lock.unlock();

            m_cleanup_in_progress = true;
            // Process the cleanup request
            process_cleanup_request(info);
            m_cleanup_in_progress = false;

            lock.lock();
        }
    }
}

void StorageCleanupServiceExt::process_cleanup_request([[maybe_unused]] const StorageInfo &info)
{

    // Use the strategy to perform cleanup
    // Here we do not need to use info, as the strategy handles it internally
    // However, we can use it in the future if needed to enhance the strategy which might require this information
    if (!m_strategy->clean_up(*this))
    {
        HAILO_ANALYTICS_LOG_ERROR("Cleanup strategy failed (partially) to process request");
        return;
    }
}
