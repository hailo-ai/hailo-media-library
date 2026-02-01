#pragma once

// General includes
#include <cstdint>
#include <memory>
#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "thumbnail_table.hpp"
#include "sql_factory.hpp"
#include "common_utils.hpp"

namespace fs = std::filesystem;

// Using declarations for pipeline types
using hailo_analytics::pipeline::AppStatus;
using hailo_analytics::pipeline::BufferPtr;
using hailo_analytics::pipeline::MetadataPtr;
using hailo_analytics::pipeline::MetadataType;
using hailo_analytics::pipeline::SizeMetadata;
using hailo_analytics::pipeline::SizeMetadataPtr;

#define THUMB_STORAGE_QUEUE_SIZE_DEFAULT 10

#define THUMB_DB_FLASH_INTERVAL_MS 1500
#define THUMB_DB_FLASH_MIN_SIZE 50

constexpr const char *THUMB_TEMP_PATH = "clip_cache_storage/thumbnail";

class ThumStorageStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    enum DBSource
    {
        DB_SOURCE_FROM_FACTORY = 0,
        DB_SOURCE_FROM_FILE = 1,
        DB_SOURCE_NOT_SUPPORTED,
    };

  private:
    DBSource m_db_source;
    std::string m_db_source_data;
    std::string m_thumb_dir;
    std::string m_thumb_cache_dir;
    std::string m_thumb_filename_prefix;
    std::shared_ptr<ThumbnailTable> m_thumb_table;
    std::vector<std::tuple<int64_t, std::string>> m_pending_thumbnails;

    // Thread-related members
    std::thread m_database_thread;
    std::atomic<bool> m_should_terminate;
    std::mutex m_data_mutex;
    std::condition_variable m_data_cv;

  public:
    inline ThumStorageStage(std::string name, DBSource db_source, std::string db_source_data, std::string thumb_dir,
                            std::string thumb_filename_prefix, size_t queue_size = THUMB_STORAGE_QUEUE_SIZE_DEFAULT,
                            bool leaky = false, bool trace_processing_operations = true)
        : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
          m_db_source(db_source), m_db_source_data(db_source_data), m_thumb_dir(thumb_dir),
          m_thumb_filename_prefix(thumb_filename_prefix), m_should_terminate(false)
    {
    }

    inline hailo_analytics::pipeline::AppStatus init() override
    {
        switch (m_db_source)
        {

        case DB_SOURCE_FROM_FILE: {
            m_thumb_table = std::make_shared<ThumbnailTable>(m_db_source_data);
            if (!m_thumb_table->open())
                return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;

            m_thumb_table->create_tables();
            break;
        }
        case DB_SOURCE_FROM_FACTORY: {
            auto thumbnail_table_result = SqlDatabaseQuickAccess::get_database(m_db_source_data);
            if (!thumbnail_table_result)
            {
                std::cerr << "Thumbnail Storage " << m_stage_name << " database not found in factory" << std::endl;
                HAILO_ANALYTICS_LOG_ERROR("thumbnail Storage {} database not found in factory", m_stage_name);
                return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
            }

            m_thumb_table = std::dynamic_pointer_cast<ThumbnailTable>(thumbnail_table_result.value());
            break;
        }
        default:
            std::cerr << "Thumbnail Storage " << m_stage_name << " unsupported faiss database source" << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("Thumbnail Storage {} unsupported faiss database source", m_stage_name);
            return hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR;
        }

        if (!FileSysUtils::ensure_directory_exists(m_thumb_dir))
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;

        // If the thumbnail mount point is not /var/volatile (memory), we create a temp cache path in /var/volatile
        if (m_thumb_dir.compare(0, std::strlen(VOLATILE_PATH), VOLATILE_PATH) != 0)
        {
            // Create temporary thumbnail storage path in memory as cache
            m_thumb_cache_dir = FileSysUtils::join_path(VOLATILE_PATH, THUMB_TEMP_PATH);
            if (!FileSysUtils::ensure_directory_exists(m_thumb_cache_dir))
                return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
        }

        // Start the database access thread
        m_database_thread = std::thread(&ThumStorageStage::database_access, this);

        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    inline hailo_analytics::pipeline::AppStatus deinit() override
    {
        // Signal the thread to terminate
        m_should_terminate = true;
        m_data_cv.notify_all();

        // Wait for the thread to finish
        if (m_database_thread.joinable())
        {
            m_database_thread.join();
        }

        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    inline hailo_analytics::pipeline::AppStatus process(hailo_analytics::pipeline::BufferPtr data)
    {
        static int64_t last_epoch_milli = 0;
        if (m_thumb_table == nullptr)
        {
            std::cerr << "Thumb Storage " << m_stage_name << " database failed to initialized" << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("Thumb Storage {} database failed to initialized", m_stage_name);
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "Thumb Storage " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("Thumb Storage {} got buffer of unknown size, add SizeMeta");
            return AppStatus::PIPELINE_ERROR;
        }

        // Generate filename based on epoch milliseconds and we make sure it's unique as sometime file saving is too
        // fast and we could endp up with same filename.
        int64_t epoch_millis = get_current_epochmilliseconds();
        epoch_millis = (epoch_millis == last_epoch_milli) ? (last_epoch_milli + 1) : epoch_millis;
        last_epoch_milli = epoch_millis;
        std::string filename = generate_epoch_filename(m_thumb_filename_prefix, epoch_millis, "jpeg");

        // Get the path to store the thumbnail
        std::string thumb_path = m_thumb_dir;
        if (!m_thumb_cache_dir.empty())
        {
            thumb_path = m_thumb_cache_dir;
        }

        // Save thumbnail to file
        std::ofstream file = create_file(thumb_path, filename);
        if (!file)
        {
            std::cerr << "Failed to open file: " << thumb_path << "/" << filename << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("Thumb Storage {} failed to open file {}/{}", m_stage_name, thumb_path, filename);
            return AppStatus::PIPELINE_ERROR;
        }

        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        if (!write_encoded_data(data->get_buffer(), size, file))
        {
            file.close();
            return AppStatus::PIPELINE_ERROR;
        }

        file.close();

        // Push to pending for batch insert
        {
            std::lock_guard<std::mutex> lock(m_data_mutex);
            m_pending_thumbnails.emplace_back(epoch_millis, m_thumb_dir + "/" + filename);
        }

        return AppStatus::SUCCESS;
    }

  private:
    // Thread execution function
    void database_access()
    {
        std::chrono::high_resolution_clock::time_point last_insert_time = std::chrono::high_resolution_clock::now();
        ;
        std::vector<std::tuple<int64_t, std::string>> thumbnails_to_process;

        while (!m_should_terminate.load())
        {
            {
                std::unique_lock<std::mutex> lock(m_data_mutex);
                // Wait for notification or timeout (to check termination flag)
                m_data_cv.wait_for(lock, std::chrono::milliseconds(100), [this] { return m_should_terminate.load(); });

                // Check if m_pending_thumbnails reaches a certain size or time in ms has past
                // we move it to thumbnails_to_process and release the data mutex lock right away
                if (m_should_terminate.load() || m_pending_thumbnails.size() >= THUMB_DB_FLASH_MIN_SIZE ||
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() -
                                                                          last_insert_time)
                            .count() >= THUMB_DB_FLASH_INTERVAL_MS)
                {
                    thumbnails_to_process = std::move(m_pending_thumbnails);
                    m_pending_thumbnails.clear();
                    last_insert_time = std::chrono::high_resolution_clock::now();
                }
            }

            // Push to Database
            if (m_thumb_table && !thumbnails_to_process.empty())
            {
                // Start measuring time
                auto start = std::chrono::high_resolution_clock::now();

                m_thumb_table->insert_batch(thumbnails_to_process);

                // DEBUG Measure
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start);
                if (duration.count() > 30)
                {
                    std::cout << "Time taken THUMB table insert: " << duration.count() << " ms"
                              << ", total insert item: " << thumbnails_to_process.size() << std::endl;
                }
            }

            // Move files from temp cache to final thumb dir
            if (!m_thumb_cache_dir.empty())
            {
                // Start measuring time
                auto start = std::chrono::high_resolution_clock::now();

                for (const auto &item : thumbnails_to_process)
                {
                    std::string final_path = std::get<1>(item);

                    auto filename = FileSysUtils::extract_file_name(final_path);

                    std::string cache_path = (fs::path(m_thumb_cache_dir) / filename).string();
                    int ret = FileSysUtils::move_file_sendfile(cache_path, final_path);
                    if (ret != 0)
                    {
                        std::cerr << "Thumb Storage " << m_stage_name << " failed to move file from " << cache_path
                                  << " to " << final_path << ", error code: " << ret << std::endl;
                        HAILO_ANALYTICS_LOG_ERROR("Thumb Storage {} failed to move file from {} to {}, error code: {}",
                                                  m_stage_name, cache_path, final_path, ret);
                    }
                }

                // DEBUG Measure
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start);
                if (duration.count() > 30)
                {
                    std::cout << "Time taken THUMB image move from cache to storage: " << duration.count() << " ms"
                              << std::endl;
                }
            }

            thumbnails_to_process.clear();

            if (m_should_terminate.load())
                break;
        }
    }

    bool write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
    {
        char *data = (char *)buffer->get_plane_ptr(0);
        if (!data)
        {
            std::cerr << "Error occurred at writing time!" << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("Thumb Storage {} write encoded data", m_stage_name);
            return false;
        }
        output_file.write(data, size);

        return true;
    }

    std::ofstream create_file(const std::string &dir_path, const std::string &filename)
    {
        fs::path full_path = fs::path(dir_path) / filename;

        // Open the file with desired flags
        std::ofstream file(full_path, std::ios::out | std::ios::binary | std::ios::app);

        return file;
    }

    int64_t get_current_epochmilliseconds()
    {
        // Get current time in milliseconds since epoch
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return millis;
    }

    std::string generate_epoch_filename(const std::string &prefix, int64_t epoch_millis, const std::string &extension)
    {
        // Construct the filename
        std::ostringstream filename;
        filename << prefix << "_" << epoch_millis << "." << extension;

        return filename.str();
    }
};

class ThumStorageStageBuild : public ThumStorageStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        DBSource m_db_source;
        std::optional<std::string> m_db_source_data;
        std::optional<std::string> m_thumb_dir;
        std::optional<std::string> m_thumb_filename_prefix;
        size_t m_queue_size = THUMB_STORAGE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_database_source(DBSource source)
        {
            m_db_source = source;
            return *this;
        }
        Builder &set_database_source_data(std::string data)
        {
            m_db_source_data = data;
            return *this;
        }
        Builder &set_thumbnail_path(std::string path)
        {
            m_thumb_dir = path;
            return *this;
        }
        Builder &set_thumbnail_file_prefix(std::string prefix)
        {
            m_thumb_filename_prefix = prefix;
            return *this;
        }
        Builder &set_queue_size_opt(size_t size)
        {
            m_queue_size = size;
            return *this;
        }
        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }
        Builder &set_trace_opt(bool activate)
        {
            m_trace = activate;
            return *this;
        }

        std::shared_ptr<ThumStorageStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING(m_db_source < DB_SOURCE_NOT_SUPPORTED, "set_database_source");
            THROW_IF_MISSING(m_db_source_data.has_value(), "set_database_source_data");
            THROW_IF_MISSING(m_thumb_dir.has_value(), "set_thumbnail_path");
            THROW_IF_MISSING(m_thumb_filename_prefix.has_value(), "set_thumbnail_file_prefix");

            return std::make_shared<ThumStorageStage>(m_stage_name.value(), m_db_source, m_db_source_data.value(),
                                                      m_thumb_dir.value(), m_thumb_filename_prefix.value(),
                                                      m_queue_size, m_leaky, m_trace);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
