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
#include <vector>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "video_table.hpp"
#include "converter/segmented_mkv_muxer.hpp"
#include "sql_factory.hpp"

// Using declarations for pipeline types
using hailo_analytics::pipeline::BufferPtr;
using hailo_analytics::pipeline::MetadataType;
using hailo_analytics::pipeline::SizeMetadata;
using hailo_analytics::pipeline::SizeMetadataPtr;

constexpr const char *VIDEO_TEMP_PATH = "clip_cache_storage/video";

#define VIDEO_STORAGE_QUEUE_SIZE_DEFAULT 50
#define VIDEO_DURATION_SECONDS_DEFAULT 15

#include <deque>
#include <cstdint>
#include <algorithm>

class VideoStorageStage : public hailo_analytics::pipeline::ThreadedStage
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
    std::string m_video_dir;
    std::string m_video_cache_dir;
    std::string m_video_filename_prefix;
    uint32_t m_video_segment_duration_seconds;
    std::shared_ptr<GStreamerMkvSegmenter> m_muxer;
    std::shared_ptr<VideoTable> m_video_table;
    bool m_enable;
    bool m_always_record;
    std::atomic<bool> m_event_captured = false;

    // Structure to hold pending video operations
    struct VideoPendingOperation
    {
        int64_t start_time_epoch_ms;
        int64_t end_time_epoch_ms;
        std::string filename;
    };

    std::vector<VideoPendingOperation> m_pending_operations;

    // Thread-related members
    std::thread m_database_thread;
    std::atomic<bool> m_should_terminate;
    std::mutex m_data_mutex;
    std::condition_variable m_data_cv;

  public:
    VideoStorageStage(std::string name, DBSource db_source, std::string db_source_data, std::string video_dir,
                      std::string video_filename_prefix,
                      uint32_t video_segment_duration_seconds = VIDEO_DURATION_SECONDS_DEFAULT, bool enable = true,
                      bool always_record = false, size_t queue_size = VIDEO_STORAGE_QUEUE_SIZE_DEFAULT,
                      bool leaky = false, bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;

    hailo_analytics::pipeline::AppStatus deinit() override;

    void loop() override;

    hailo_analytics::pipeline::AppStatus process(BufferPtr data);

  private:
    static void segment_mkv_callback(const char *filename, uint32_t duration_ms, uint64_t start_time_epoch_ms,
                                     [[maybe_unused]] uint32_t segment_index, void *user_data);

    static void mkv_callback_debug(const char *filename, uint32_t duration_ms, uint64_t start_time_epoch_ms,
                                   uint32_t segment_index);

    // Thread execution function
    void database_access();
};

class VideoStorageStageBuild : public VideoStorageStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        DBSource m_db_source;
        std::optional<std::string> m_db_source_data;
        std::optional<std::string> m_video_dir;
        std::optional<std::string> m_video_filename_prefix;
        uint32_t m_video_segment_duration_seconds = VIDEO_DURATION_SECONDS_DEFAULT;
        bool m_enable = true;
        bool m_always_record = false;
        size_t m_queue_size = VIDEO_STORAGE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_database_source(DBSource source);
        Builder &set_database_source_data(std::string data);
        Builder &set_video_path(std::string path);
        Builder &set_video_file_prefix(std::string prefix);
        Builder &set_video_segment_duration(size_t seconds);
        Builder &set_enable(bool enable);
        Builder &set_always_record(bool always_record);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_trace_opt(bool activate);

        std::shared_ptr<VideoStorageStage> buildptr() const;
    };

    static Builder create();
};
