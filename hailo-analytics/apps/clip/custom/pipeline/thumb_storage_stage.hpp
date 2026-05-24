#pragma once

#include <stddef.h>
#include <media_library/buffer_pool.hpp>
// General includes
#include <cstdint>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <tuple>
#include <vector>

#include "media_library/cloexec_fstream.hpp"
// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "thumbnail_table.hpp"

namespace fs = std::filesystem;

// Using declarations for pipeline types
using hailo_analytics::pipeline::AppStatus;
using hailo_analytics::pipeline::BufferPtr;
using hailo_analytics::pipeline::MetadataPtr;
using hailo_analytics::pipeline::MetadataType;
using hailo_analytics::pipeline::SizeMetadata;
using hailo_analytics::pipeline::SizeMetadataPtr;

#define THUMB_STORAGE_QUEUE_SIZE_DEFAULT 10

#define THUMB_DB_FLASH_INTERVAL_MS 4000
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
    ThumStorageStage(std::string name, DBSource db_source, std::string db_source_data, std::string thumb_dir,
                     std::string thumb_filename_prefix, size_t queue_size = THUMB_STORAGE_QUEUE_SIZE_DEFAULT,
                     bool leaky = false, bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;

    hailo_analytics::pipeline::AppStatus deinit() override;

    hailo_analytics::pipeline::AppStatus process(hailo_analytics::pipeline::BufferPtr data);

  private:
    // Thread execution function
    void database_access();

    bool write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, cloexec::ofstream &output_file);

    cloexec::ofstream create_file(const std::string &dir_path, const std::string &filename);

    int64_t get_current_epochmilliseconds();

    std::string generate_epoch_filename(const std::string &prefix, int64_t epoch_millis, const std::string &extension);
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
        Builder &set_stage_name(std::string name);
        Builder &set_database_source(DBSource source);
        Builder &set_database_source_data(std::string data);
        Builder &set_thumbnail_path(std::string path);
        Builder &set_thumbnail_file_prefix(std::string prefix);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<ThumStorageStage> buildptr() const;
    };

    static Builder create();
};
