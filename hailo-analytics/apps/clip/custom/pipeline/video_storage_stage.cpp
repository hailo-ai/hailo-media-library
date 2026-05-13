#include "video_storage_stage.hpp"

#include "hailo_analytics/pipeline/core/error_utils.hpp"

#include <chrono>
#include <iomanip>

VideoStorageStage::VideoStorageStage(std::string name, DBSource db_source, std::string db_source_data,
                                     std::string video_dir, std::string video_filename_prefix,
                                     uint32_t video_segment_duration_seconds, bool enable, bool always_record,
                                     size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_db_source(db_source), m_db_source_data(db_source_data), m_video_dir(video_dir),
      m_video_filename_prefix(video_filename_prefix), m_video_segment_duration_seconds(video_segment_duration_seconds),
      m_enable(enable), m_always_record(always_record), m_should_terminate(false)
{
}

hailo_analytics::pipeline::AppStatus VideoStorageStage::init()
{
    switch (m_db_source)
    {

    case DB_SOURCE_FROM_FILE: {
        m_video_table = std::make_shared<VideoTable>(m_db_source_data);
        if (!m_video_table->open())
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;

        m_video_table->create_tables();
        break;
    }
    case DB_SOURCE_FROM_FACTORY: {
        auto video_table_result = SqlDatabaseQuickAccess::get_database(m_db_source_data);
        if (!video_table_result)
        {
            HAILO_ANALYTICS_LOG_ERROR("Video Storage {} database not found in factory", m_stage_name);
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
        }

        m_video_table = std::dynamic_pointer_cast<VideoTable>(video_table_result.value());
        break;
    }
    default:
        HAILO_ANALYTICS_LOG_ERROR("Video Storage {} unsupported faiss database source", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR;
    }

    if (!FileSysUtils::ensure_directory_exists(m_video_dir))
        return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;

    std::string video_path = m_video_dir;

    // If the video mount point is not /var/volatile (memory), we create a temp cache path in /var/volatile
    if (m_video_dir.compare(0, std::strlen(VOLATILE_PATH), VOLATILE_PATH) != 0)
    {
        // Create temporary video storage path in memory as cache
        m_video_cache_dir = FileSysUtils::join_path(VOLATILE_PATH, VIDEO_TEMP_PATH);
        if (!FileSysUtils::ensure_directory_exists(m_video_cache_dir))
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;

        video_path = m_video_cache_dir;

        // Clean up any old cached files on init
        auto cached_files = FileSysUtils::get_all_file_names(m_video_cache_dir, true);
        if (!cached_files.empty())
            FileSysUtils::delete_files(cached_files);
    }

    // Initialize segmenter
    m_muxer = std::make_shared<GStreamerMkvSegmenter>(CodecType::H264, video_path, m_video_filename_prefix,
                                                      m_video_segment_duration_seconds);

    if (!m_muxer->initialize() || !m_muxer->start())
    {
        HAILO_ANALYTICS_LOG_ERROR("Video Storage {} failed to initialize and start muxer", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
    }

    m_muxer->set_segment_notification_callback(segment_mkv_callback, this);

    // Start the database access thread
    m_database_thread = std::thread(&VideoStorageStage::database_access, this);

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus VideoStorageStage::deinit()
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

void VideoStorageStage::loop()
{
    while (!m_end_of_stream)
    {
        // the first queue is the one that is condisidered the "main stream"
        BufferPtr main_buffer = m_queues[0]->pop();
        if (main_buffer == nullptr && m_end_of_stream)
        {
            break;
        }

        process(main_buffer);

        // Set the event captured for video storage decision
        if (m_queues.size() > 1)
        {
            while (m_queues[1]->size())
            {
                m_event_captured = true;
                m_queues[1]->pop();
            }
        }
        else // If there is no second queue, we save all recorded video regardless if there is event or not
        {
            m_event_captured = true;
        }
    }
}

hailo_analytics::pipeline::AppStatus VideoStorageStage::process(BufferPtr data)
{

    if (m_enable)
    {
        if (m_video_table == nullptr)
        {
            HAILO_ANALYTICS_LOG_ERROR("Video Storage {} database failed to initialize", m_stage_name);
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
        }

        auto metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.empty())
        {
            HAILO_ANALYTICS_LOG_ERROR("video storage {} got buffer of unknown size, add SizeMeta", m_stage_name);
            return hailo_analytics::pipeline::AppStatus::PIPELINE_ERROR;
        }

        auto size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();

        m_muxer->feed_frame((uint8_t *)data->get_buffer()->get_plane_ptr(0), size, data->get_buffer()->pts);
    }

    send_to_subscribers(data);

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

void VideoStorageStage::segment_mkv_callback(const char *filename, uint32_t duration_ms, uint64_t start_time_epoch_ms,
                                             [[maybe_unused]] uint32_t segment_index, void *user_data)
{

    // mkv_callback_debug(filename, duration_ms, start_time_epoch_ms, segment_index);

    // Save to Database using asynchronous thread
    VideoStorageStage *video_storage_stage = static_cast<VideoStorageStage *>(user_data);

    // We keep the recorded video file if there is event (or always_record is set), otherwise we delete the file
    if (video_storage_stage->m_always_record || video_storage_stage->m_event_captured.load())
    {
        video_storage_stage->m_event_captured = false;
    }
    else
    {
        FileSysUtils::delete_files(std::vector<std::string>{std::string(filename)});
        return;
    }

    // Add to pending operations
    {
        std::lock_guard<std::mutex> lock(video_storage_stage->m_data_mutex);
        video_storage_stage->m_pending_operations.emplace_back(
            VideoPendingOperation{static_cast<int64_t>(start_time_epoch_ms),
                                  static_cast<int64_t>(start_time_epoch_ms + duration_ms), std::string(filename)});
    }

    // Notify the database thread
    video_storage_stage->m_data_cv.notify_one();
}

void VideoStorageStage::mkv_callback_debug(const char *filename, uint32_t duration_ms, uint64_t start_time_epoch_ms,
                                           uint32_t segment_index)
{
    std::cout << "Segment completed: " << filename << std::endl;
    std::cout << "Duration: " << duration_ms << " ms"
              << "(" << duration_ms / 1000.0 << " seconds)" << std::endl;
    std::cout << "Start time (epoch ms): " << start_time_epoch_ms << std::endl;
    std::cout << "Segment index: " << segment_index << std::endl;

    // Convert epoch to human readable time
    time_t start_time_sec = start_time_epoch_ms / 1000;
    struct tm *tm_info = localtime(&start_time_sec);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    std::cout << "Start time: " << time_str << "." << std::setfill('0') << std::setw(3)
              << static_cast<uint32_t>(start_time_epoch_ms % 1000) << std::endl;
}

void VideoStorageStage::database_access()
{
    std::chrono::high_resolution_clock::time_point last_insert_time = std::chrono::high_resolution_clock::now();
    std::vector<VideoPendingOperation> operations_to_process;

    while (!m_should_terminate.load())
    {
        {
            std::unique_lock<std::mutex> lock(m_data_mutex);
            // Wait for notification or timeout (to check termination flag)
            m_data_cv.wait_for(lock, std::chrono::milliseconds(100),
                               [this] { return m_should_terminate.load() || !m_pending_operations.empty(); });

            // Check if m_pending_operations has data or we should terminate
            // we move it to operations_to_process and release the data mutex lock right away
            if (m_should_terminate.load() || !m_pending_operations.empty())
            {
                operations_to_process = std::move(m_pending_operations);
                m_pending_operations.clear();
                last_insert_time = std::chrono::high_resolution_clock::now();
            }
        }

        if (operations_to_process.empty())
            continue;

        // Update file paths if using cache directory
        for (auto &item : operations_to_process)
        {
            if (!m_video_cache_dir.empty())
            {
                std::string cache_filepath = item.filename;
                auto filename = FileSysUtils::extract_file_name(cache_filepath);

                std::string dest_filepath = (fs::path(m_video_dir) / filename).string();
                item.filename = dest_filepath;
            }
        }

        if (m_video_table)
        {
            // Start measuring time
            auto start = std::chrono::high_resolution_clock::now();

            // Use batchInsert with all pending records
            std::vector<std::tuple<int64_t, int64_t, std::string>> batch_records;
            for (const auto &operation : operations_to_process)
            {
                batch_records.emplace_back(operation.start_time_epoch_ms, operation.end_time_epoch_ms,
                                           operation.filename);
            }

            m_video_table->insert_batch(batch_records);

            // DEBUG Measure
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            if (duration.count() > 30)
            {
                HAILO_ANALYTICS_LOG_INFO("Time taken VIDEO table batch insert: {} ms, total insert items: {}",
                                         duration.count(), operations_to_process.size());
            }
        }

        // Move files from temp cache to final video dir
        if (!m_video_cache_dir.empty())
        {
            // Start measuring time
            auto start = std::chrono::high_resolution_clock::now();

            for (const auto &item : operations_to_process)
            {
                std::string dest_filepath = item.filename;

                auto filename = FileSysUtils::extract_file_name(dest_filepath);

                std::string cache_filepath = (fs::path(m_video_cache_dir) / filename).string();
                int ret = FileSysUtils::move_file_sendfile(cache_filepath, dest_filepath);
                if (ret != 0)
                {
                    HAILO_ANALYTICS_LOG_ERROR("Video Storage {} failed to move file from {} to {}, error code: {}",
                                              m_stage_name, cache_filepath, dest_filepath, ret);
                }
            }

            // DEBUG Measure
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            if (duration.count() > 30)
            {
                HAILO_ANALYTICS_LOG_INFO("Time taken Video files move from cache to storage: {} ms", duration.count());
            }
        }

        operations_to_process.clear();

        if (m_should_terminate.load())
            break;
    }
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_database_source(DBSource source)
{
    m_db_source = source;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_database_source_data(std::string data)
{
    m_db_source_data = data;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_video_path(std::string path)
{
    m_video_dir = path;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_video_file_prefix(std::string prefix)
{
    m_video_filename_prefix = prefix;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_video_segment_duration(size_t seconds)
{
    m_video_segment_duration_seconds = seconds;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_enable(bool enable)
{
    m_enable = enable;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_always_record(bool always_record)
{
    m_always_record = always_record;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

VideoStorageStageBuild::Builder &VideoStorageStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<VideoStorageStage> VideoStorageStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_db_source < DB_SOURCE_NOT_SUPPORTED, "set_database_source");
    THROW_IF_MISSING(m_db_source_data.has_value(), "set_database_source_data");
    THROW_IF_MISSING(m_video_dir.has_value(), "set_video_path");
    THROW_IF_MISSING(m_video_filename_prefix.has_value(), "set_video_file_prefix");
    THROW_IF_MISSING(m_video_segment_duration_seconds >= 5, "set_video_segment_duration");

    return std::make_shared<VideoStorageStage>(m_stage_name.value(), m_db_source, m_db_source_data.value(),
                                               m_video_dir.value(), m_video_filename_prefix.value(),
                                               m_video_segment_duration_seconds, m_enable, m_always_record,
                                               m_queue_size, m_leaky, m_trace);
}

VideoStorageStageBuild::Builder VideoStorageStageBuild::create()
{
    return Builder();
}
