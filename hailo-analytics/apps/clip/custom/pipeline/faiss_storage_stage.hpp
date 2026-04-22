#pragma once

// General includes
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <utility>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

// Infra includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "faiss_table.hpp"
#include "faiss_factory.hpp"
#include "sql_factory.hpp"

namespace fs = std::filesystem;

// Using declarations for pipeline types
using hailo_analytics::pipeline::AppStatus;
using hailo_analytics::pipeline::BufferPtr;

#define FAISS_STORAGE_QUEUE_SIZE_DEFAULT 15

#define FAISS_DB_FLASH_INTERVAL_MS 1000
#define FAISS_DB_FLASH_MIN_SIZE 50

class FaissStorageStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    enum FaissIndexSource
    {
        IDX_SOURCE_FROM_USER_META = 0,
        IDX_SOURCE_FROM_FILE = 1,
        IDX_SOURCE_NOT_SUPPORTED,
    };

    enum DBSource
    {
        DB_SOURCE_FROM_FACTORY = 0,
        DB_SOURCE_FROM_FILE = 1,
        DB_SOURCE_NOT_SUPPORTED,
    };

  private:
    std::string m_db_path;
    std::string m_db_factory_name;
    std::string m_faiss_dir;
    std::string m_faiss_filename_prefix;
    std::shared_ptr<FaissTable> m_faiss_table;
    FaissIndexSource m_faiss_index_source;
    DBSource m_db_source;

    std::vector<std::tuple<int64_t, int32_t, int64_t, std::string, std::string>> m_pending_operations;

    // Thread-related members
    std::thread m_database_thread;
    std::atomic<bool> m_should_terminate;
    std::mutex m_data_mutex;
    std::condition_variable m_data_cv;

  public:
    FaissStorageStage(std::string name, std::string db_path, std::string faiss_dir, std::string faiss_filename_prefix,
                      size_t queue_size = FAISS_STORAGE_QUEUE_SIZE_DEFAULT, bool leaky = false,
                      bool trace_processing_operations = true);

    FaissStorageStage(std::string name, std::string db_factory_name,
                      size_t queue_size = FAISS_STORAGE_QUEUE_SIZE_DEFAULT, bool leaky = false,
                      bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;

    hailo_analytics::pipeline::AppStatus deinit() override;

    void loop() override;

    hailo_analytics::pipeline::AppStatus process(BufferPtr data);

  private:
    // Thread execution function
    void database_access();

    std::ofstream create_file(const std::string &dir_path, const std::string &filename);

    int64_t get_current_epochmilliseconds();

    std::string generate_filename(const std::string &prefix, const std::string &extension);

    std::pair<std::string, FaissDatabaseQuickAccess::DatabasePtr> get_faiss_index_from_user_meta(HailoROIPtr roi);
};

class FaissStorageStageBuild : public FaissStorageStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_db_path;
        std::optional<std::string> m_db_factory_name;
        std::optional<std::string> m_faiss_dir;
        std::optional<std::string> m_faiss_filename_prefix;
        FaissIndexSource m_faiss_index_source = FaissIndexSource::IDX_SOURCE_NOT_SUPPORTED;
        DBSource m_db_source = DBSource::DB_SOURCE_NOT_SUPPORTED;
        size_t m_queue_size = FAISS_STORAGE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_database_file_path(std::string filepath);
        Builder &set_db_factory_name(std::string name);
        Builder &set_faiss_path(std::string path);
        Builder &set_faiss_file_prefix(std::string prefix);
        Builder &set_faiss_index_source(FaissIndexSource source);
        Builder &set_db_source(DBSource source);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<FaissStorageStage> buildptr() const;
    };

    static Builder create();
};
