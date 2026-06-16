#include "storage_cleanup_strategy.hpp"

#include <faiss/MetricType.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <exception>

#include "faiss_factory.hpp"
#include "common_utils.hpp"
#include "faiss_partitioned.hpp"

FaissShardFirstCleanupStrategy::FaissShardFirstCleanupStrategy(float percent)
{
    m_cleanup_param = percent;
}
#include "faiss_table.hpp"
#include "thumbnail_table.hpp"
#include "video_table.hpp"

bool FaissShardFirstCleanupStrategy::clean_up(StorageCleanupServiceExt &cleanup_service)
{
    bool success = true;

    // Ensure the cleanup_param is a float
    float cleanup_param_float = 5.0f; // Remove 5% of the oldest video files by default
    if (auto percent = std::get_if<float>(&m_cleanup_param))
    {
        cleanup_param_float = (*percent > 100.0f) ? 100.0f : *percent;
    }

    // We first obtain how many different Faiss index is currently running
    auto faiss_index_names = FaissDatabaseQuickAccess::get_all_database_names();

    HAILO_ANALYTICS_LOG_INFO("Faiss indices found: {}", faiss_index_names.size());
    for (const auto &name : faiss_index_names)
    {
        HAILO_ANALYTICS_LOG_INFO(" - {}", name);
    }

    // Now for each faiss index name we retrieve the shard file that belongs to the faiss and
    // start removing all its related assets.
    for (const auto &network_name : faiss_index_names)
    {
        // Get Faiss index database config
        auto config_result = FaissDatabaseQuickAccess::get_database_config(network_name);
        if (!config_result)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to get config for Faiss index '{}': {}", network_name,
                                      config_result.error().message);
            continue;
        }

        HAILO_ANALYTICS_LOG_INFO("Processing cleanup for Faiss index: {} with db directory: {} and file prefix: {}",
                                 network_name, config_result.value()->db_directory, config_result.value()->file_prefix);

        auto file_names = FileSysUtils::get_all_file_names(config_result.value()->db_directory, true,
                                                           config_result.value()->file_prefix);

        size_t num_files_to_remove = static_cast<size_t>(std::ceil(file_names.size() * cleanup_param_float / 100.0f));

        for (size_t i = 0; i < num_files_to_remove; ++i)
        {
            /* Lets handle faiss table db */

            // Start measuring time
            auto start = std::chrono::high_resolution_clock::now();

            // Retrieve the Faiss index db to get all the faiss ids from the shard file
            auto faiss_idx_result = FaissDatabaseQuickAccess::get_database(network_name);
            if (!faiss_idx_result)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to get Faiss database for index: {}", network_name);
                continue;
            }

            FaissDatabaseQuickAccess::DatabasePtr faiss_indx_db = faiss_idx_result.value();
            auto shard_file = file_names[i];
            auto shard_ids_result = faiss_indx_db->get_all_ids_from_shard(shard_file);
            if (!FaissDatabaseQuickAccess::handle_faiss_result(shard_ids_result,
                                                               "Failed to get all IDs from shard for file"))
            {
                continue;
            }

            HAILO_ANALYTICS_LOG_INFO("FaissShardFirstCleanupStrategy Processing cleanup request for network: {} "
                                     "file: {} with total faiss ids: {}",
                                     network_name, file_names[i], shard_ids_result.value().size());

            // Gather all faiss id by network name to prepare for deletion
            // also we keep the largest id to retrieve the latest timestamp
            std::vector<std::pair<int64_t, std::string>> faiss_ids;
            int64_t latest_timestamp_faiss_id = 0;
            for (const auto &id : shard_ids_result.value())
            {
                faiss_ids.emplace_back(id, network_name);
                latest_timestamp_faiss_id = std::max(latest_timestamp_faiss_id, id);
            }

            // Lets retireve the timestamp of latest_timestamp_faiss_id's network_name
            int64_t latest_timestamp = 0;
            auto latest_timestamp_result =
                cleanup_service.get_faiss_table()->query_timestamp(latest_timestamp_faiss_id, network_name);
            if (!latest_timestamp_result.has_value())
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to get latest timestamp for faiss id: {} network: {}",
                                          latest_timestamp_faiss_id, network_name);
                continue;
            }
            latest_timestamp = latest_timestamp_result.value().timestamp;

            // Delete all faiss records from faiss database table
            if (faiss_ids.size() && !cleanup_service.get_faiss_table()->delete_batch_by_faiss_ids(faiss_ids))
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to delete faiss record db table");
            }

            /* Now lets handle thumbnails */

            auto thumbnails = cleanup_service.get_thumbnail_table()->get_records_by_timestamp_before(latest_timestamp);

            // Gather all ThumbnailRecord path into a vector for deletion
            std::vector<std::string> thumbnail_paths;
            for (const auto &thumbnail : thumbnails)
            {
                thumbnail_paths.push_back(thumbnail.path);
            }

            // Print total file to be removed and also print the first and the last file path
            HAILO_ANALYTICS_LOG_INFO("Total thumbnails to delete: {}", thumbnail_paths.size());
            if (!thumbnail_paths.empty())
            {
                HAILO_ANALYTICS_LOG_INFO("First thumbnail to delete: {}", thumbnail_paths.front());
                HAILO_ANALYTICS_LOG_INFO("Last thumbnail to delete: {}", thumbnail_paths.back());
            }

            // Delete thumbnails files by path
            FileSysUtils::delete_files(thumbnail_paths);

            // Delete thumbnails record from database table
            int total_tumb_table_record_del =
                cleanup_service.get_thumbnail_table()->delete_by_timestamp_range(0, latest_timestamp);
            if (total_tumb_table_record_del < 0)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to delete thumbnail records from database table");
            }
            else
            {
                HAILO_ANALYTICS_LOG_INFO("Deleted thumbnail records from database table: {}",
                                         total_tumb_table_record_del);
            }

            /* Now lets handle video */

            auto videos = cleanup_service.get_video_table()->get_records_by_timestamp_range(0, latest_timestamp);
            std::vector<std::string> video_paths;
            for (const auto &video : videos)
            {
                video_paths.push_back(video.path);
            }

            // Print total file to be removed and also print the first and the last file path
            HAILO_ANALYTICS_LOG_INFO("Total videos to delete: {}", video_paths.size());
            if (!video_paths.empty())
            {
                HAILO_ANALYTICS_LOG_INFO("First video to delete: {}", video_paths.front());
                HAILO_ANALYTICS_LOG_INFO("Last video to delete: {}", video_paths.back());
            }

            // Delete video files by path
            FileSysUtils::delete_files(video_paths);

            // Delete video record from database table
            int total_video_table_record_del =
                cleanup_service.get_video_table()->delete_by_timestamp_range(0, latest_timestamp);

            if (total_video_table_record_del < 0)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to delete video records from database table");
            }
            else
            {
                HAILO_ANALYTICS_LOG_INFO("Deleted video records from database table: {}", total_video_table_record_del);
            }

            /* Now we handle the faiss index shard file*/

            // We delete the faiss shard file
            if (!FileSysUtils::delete_files({file_names[i]}))
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to delete faiss index file (maybe already deleted): {}",
                                          file_names[i]);
            }
            HAILO_ANALYTICS_LOG_INFO("Deleted faiss index file: {}", file_names[i]);

            // Update by removing the partition from the FAISS index
            faiss_indx_db->remove_partition(file_names[i]);

            // AARON DEBUG Measure
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            HAILO_ANALYTICS_LOG_INFO("Time taken STORAGE CLEANUP: {} ms", duration.count());

            // Sleep for 100ms for each cycle
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    return success;
}
