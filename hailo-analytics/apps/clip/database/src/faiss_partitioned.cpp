#include "faiss_partitioned.hpp"

// TaskFutureRunner implementation
TaskFutureRunner::TaskFutureRunner() : m_stop(false)
{
    m_worker = std::thread([this] { run(); });
}

TaskFutureRunner::~TaskFutureRunner()
{
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void TaskFutureRunner::run()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
            if (m_stop && m_tasks.empty())
                return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task(); // run synchronously in the worker thread
    }
}

// PartitionedFaissDB implementation
PartitionedFaissDB::Result<std::unique_ptr<PartitionedFaissDB>> PartitionedFaissDB::create(
    int dimension, const std::string &db_directory, const std::string &file_prefix,
    const std::vector<std::string> &initial_cold_files, bool auto_shard_seal, size_t hot_shard_max_size)
{
    auto db = std::unique_ptr<PartitionedFaissDB>(
        new PartitionedFaissDB(dimension, db_directory, file_prefix, auto_shard_seal, hot_shard_max_size));

    for (const auto &file_path : initial_cold_files)
    {
        auto shard_result = FaissShard::create(dimension, file_path);
        if (!shard_result)
        {
            // We might have corrupted shard file if system was interrupted during write
            // Therefore we will log a warning and continue to load other shards
            std::cout << "WARNING: Error loading initial cold shard: " << file_path << " ("
                      << shard_result.error().message << ")" << ", will ignore this file and continue" << std::endl;
            continue;
        }
        db->m_shard_lookup_map[file_path] = shard_result.value();
        db->m_cold_shards.push_back(shard_result.value());
    }

    faiss::idx_t max_id = 0;
    for (const auto &shard : db->m_cold_shards)
    {
        auto ids = shard->get_all_ids();
        db->m_global_used_ids.insert(ids.begin(), ids.end());
        if (!ids.empty())
        {
            max_id = std::max(max_id, *std::max_element(ids.begin(), ids.end()));
        }
    }

    db->m_next_auto_id.store(max_id > 0 ? max_id + 1 : 0);

    db->create_new_hot_shard();
    db->rebuild_search_index();

    HAILO_ANALYTICS_LOG_INFO("Partitioned DB created. Loaded: {} cold shards.", db->m_cold_shards.size());
    return db;
}

PartitionedFaissDB::~PartitionedFaissDB()
{
    m_terminating = true;
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
    if (m_hot_shard && m_hot_shard->size() > 0)
    {
        seal_hot_shard();
    }
    cleanup_finished_futures(true);
}

PartitionedFaissDB::Result<faiss::idx_t> PartitionedFaissDB::insert(const std::vector<float> &vector)
{
    faiss::idx_t id = m_next_auto_id.fetch_add(1);
    auto result = insert(vector, id);
    if (result)
    {
        return id;
    }
    else
    {
        m_next_auto_id.fetch_sub(1);
        return tl::unexpected(result.error());
    }
}

PartitionedFaissDB::Result<std::vector<PartitionedFaissDB::SearchResult>> PartitionedFaissDB::search(
    const std::vector<float> &query, int k) const
{
    // Make a thread-safe copy of the search_index pointer. This is the key fix.
    // This ensures that even if another thread rebuilds the index, this thread's search
    // will complete on a consistent and valid version.

    bool use_hot_shard_directly;
    std::shared_ptr<FaissShard> hot_shard_snapshot;
    std::shared_ptr<faiss::IndexShards> search_index_snapshot;

    // NOTE: FAISS IndexShards will not work well on search when there is only one shard (hot shard)
    //       therefore a simple work around it to use the hot shard directly when there is no cold shard
    {
        std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
        if (m_cold_shards.empty())
        {
            use_hot_shard_directly = true;
            hot_shard_snapshot = m_hot_shard; // Take a snapshot of the hot shard pointer
        }
        else
        {
            use_hot_shard_directly = false;
            search_index_snapshot = m_search_index; // Take a snapshot of the search index pointer
        }
    }

    if (use_hot_shard_directly)
    {
        // Case 1: Search the hot shard directly.

        if (!hot_shard_snapshot)
            return std::vector<SearchResult>{};
        return hot_shard_snapshot->search(query, k); // Use the new helper method
    }
    else
    {
        // Case 2: Search the master IndexShards object.

        if (!search_index_snapshot || search_index_snapshot->ntotal == 0)
        {
            return std::vector<SearchResult>{};
        }

        try
        {
            int effective_k = std::min(k, static_cast<int>(search_index_snapshot->ntotal));
            std::vector<float> distances(effective_k);
            std::vector<faiss::idx_t> labels(effective_k);
            search_index_snapshot->search(1, query.data(), effective_k, distances.data(), labels.data());

            std::vector<SearchResult> results;
            results.reserve(effective_k);
            for (int i = 0; i < effective_k; ++i)
            {
                if (labels[i] != -1)
                    results.emplace_back(labels[i], distances[i]);
            }
            return results;
        }
        catch (const std::exception &e)
        {
            return tl::unexpected(Error(ErrorType::FAISS_ERROR, "Search failed: " + std::string(e.what())));
        }
    }
}

PartitionedFaissDB::Result<PartitionedFaissDB::GetResult> PartitionedFaissDB::get_vector_by_id(faiss::idx_t id) const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);

    if (m_hot_shard && m_hot_shard->exists(id))
    {
        auto vec_res = m_hot_shard->get_vector_by_id(id);
        if (vec_res)
            return GetResult{std::move(vec_res.value()), "hot_shard"};
    }

    for (const auto &shard : m_cold_shards)
    {
        if (shard->exists(id))
        {
            auto vec_res = shard->get_vector_by_id(id);
            if (vec_res)
            {
                const auto &path = shard->get_file_path();
                return GetResult{std::move(vec_res.value()), path.empty() ? "sealing_shard" : path};
            }
        }
    }

    return tl::unexpected(Error(ErrorType::ID_NOT_FOUND, "ID not found in any shard: " + std::to_string(id)));
}

PartitionedFaissDB::Result<std::vector<faiss::idx_t>> PartitionedFaissDB::get_all_ids_from_shard(
    const std::string &filename) const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);

    auto it = m_shard_lookup_map.find(filename);
    if (it == m_shard_lookup_map.end())
    {
        return tl::unexpected(Error(ErrorType::FILE_NOT_FOUND, "Shard filename not found: " + filename));
    }
    return it->second->get_all_ids();
}

bool PartitionedFaissDB::exists(faiss::idx_t id) const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    return m_global_used_ids.count(id) > 0;
}

size_t PartitionedFaissDB::size() const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    return m_global_used_ids.size();
}

bool PartitionedFaissDB::flush()
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);

    if (m_hot_shard && m_hot_shard->size() > 0)
    {
        seal_hot_shard();
    }

    return true;
}

bool PartitionedFaissDB::remove_partition(const std::string &filename)
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);

    auto it = m_shard_lookup_map.find(filename);
    if (it == m_shard_lookup_map.end())
    {
        std::cerr << "Cannot remove partition: file not found in database map: " << filename << std::endl;
        return false;
    }

    std::shared_ptr<FaissShard> shard_to_remove_ptr = it->second;

    // Get IDs before we remove the shard from collections
    auto ids_to_remove = shard_to_remove_ptr->get_all_ids();

    // Remove from C++ management collections
    m_shard_lookup_map.erase(it);
    m_cold_shards.erase(std::remove(m_cold_shards.begin(), m_cold_shards.end(), shard_to_remove_ptr),
                        m_cold_shards.end());

    // Update the global ID set
    for (faiss::idx_t id : ids_to_remove)
    {
        m_global_used_ids.erase(id);
    }

    // Atomically rebuild the search index to reflect the removal
    rebuild_search_index();

    HAILO_ANALYTICS_LOG_INFO("Successfully removed partition: {}. It contained {} vectors.", filename,
                             ids_to_remove.size());
    return true;
}

void PartitionedFaissDB::register_seal_callback(SealCallback callback)
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
    m_seal_callback = std::move(callback);
}

PartitionedFaissDB::Result<bool> PartitionedFaissDB::generate_random_embeddings(size_t num_vectors)
{
    if (num_vectors <= 0)
    {
        return tl::unexpected(Error(ErrorType::INVALID_DIMENSION, "Number of vectors must be positive"));
    }

    if (size() >= num_vectors)
    {
        return true; // No need to generate
    }

    std::vector<float> random_vector(m_dimension);

    for (size_t i = 0; i < num_vectors; ++i)
    {
        // Generate random vector
        for (int j = 0; j < m_dimension; ++j)
        {
            random_vector[j] = static_cast<float>(rand()) / RAND_MAX; // Normalize to [0, 1]
        }

        insert(random_vector);
    }

    // Flush the remaining if auto-flush is on, if auto-flush is off then flush it all to the same shard/partition
    flush();

    return true; // Successfully generated random embeddings
}

PartitionedFaissDB::PartitionedFaissDB(int dimension, const std::string &db_dir, const std::string &prefix,
                                       bool auto_seal, size_t max_size)
    : m_next_auto_id(0), m_dimension(dimension), m_db_directory(db_dir), m_file_prefix(prefix),
      m_auto_shard_seal(auto_seal), m_hot_shard_max_size(max_size)
{
}

PartitionedFaissDB::Result<void> PartitionedFaissDB::insert(const std::vector<float> &vector, faiss::idx_t id)
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);

    if (m_global_used_ids.count(id))
    {
        return tl::unexpected(Error(ErrorType::ID_ALREADY_EXISTS, "ID already exists: " + std::to_string(id)));
    }

    auto insert_res = m_hot_shard->insert(vector, id);
    if (!insert_res)
        return tl::unexpected(insert_res.error());

    m_global_used_ids.insert(id);

    auto current_time = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = current_time - m_hot_shard_start_time;

    if (m_auto_shard_seal &&
        (m_hot_shard->size() >= m_hot_shard_max_size || elapsed_seconds.count() >= HOT_SHARD_MAX_ALLOWED_DURATION_SEC))
    {
        HAILO_ANALYTICS_LOG_INFO("Hot shard threshold reached. Sealing... Total Searchable vector: {}",
                                 m_search_index->ntotal);
        seal_hot_shard();
    }

    return {};
}

void PartitionedFaissDB::seal_hot_shard()
{
    static int64_t previous_end_time_ms = 0;
    auto old_hot_start_time = m_hot_shard_start_time;
    auto end_time = std::chrono::system_clock::now();

    cleanup_finished_futures(false);

    // Update our search index:
    // Remove the hot shard from the search index, move it to cold shards, add it back to the search index,
    // then create a new hot shard, and add it to the search index
    m_search_index->remove_shard(m_hot_shard->get_index());
    auto shard_to_seal = std::move(m_hot_shard);
    m_cold_shards.push_back(shard_to_seal);
    m_search_index->add_shard(shard_to_seal->get_index());
    create_new_hot_shard();
    m_search_index->add_shard(m_hot_shard->get_index());

    int64_t start_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(old_hot_start_time.time_since_epoch()).count();
    int64_t end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time.time_since_epoch()).count();

    // Make sure we do not have repeated file name in case the m_hot_shard_max_size is very small and
    // we reach sealing more than one shard within the same millisecond.
    if (previous_end_time_ms == end_time_ms)
    {
        end_time_ms += 1;
        previous_end_time_ms = end_time_ms;
    }

    m_sealing_futures.push_back(m_sealing_worker.enqueue(&PartitionedFaissDB::save_cold_shard, this, shard_to_seal,
                                                         start_time_ms, end_time_ms));
}

void PartitionedFaissDB::create_new_hot_shard()
{
    m_hot_shard = FaissShard::create(m_dimension).value();
    m_hot_shard_start_time = std::chrono::system_clock::now();
}

void PartitionedFaissDB::rebuild_search_index()
{
    bool parallel_search = true;
    bool successive_ids = false; // Dont force reassigning ids, use the id as is in the shards
    auto new_search_index = std::make_shared<faiss::IndexShards>(m_dimension, parallel_search, successive_ids);
    for (const auto &shard : m_cold_shards)
    {
        new_search_index->add_shard(shard->get_index());
    }
    if (m_hot_shard)
    {
        new_search_index->add_shard(m_hot_shard->get_index());
    }
    m_search_index = new_search_index;
    HAILO_ANALYTICS_LOG_INFO("Search index rebuilt. Total vectors searchable: {}", m_search_index->ntotal);
}

void PartitionedFaissDB::save_cold_shard(std::shared_ptr<FaissShard> shard_to_save, int64_t start_time_ms,
                                         int64_t end_time_ms)
{
    std::string filename = m_db_directory + "/" + m_file_prefix + "_" + std::to_string(end_time_ms) + ".faiss";

    shard_to_save->set_file_path(filename);
    auto flush_result = shard_to_save->flush();

    if (!flush_result)
    {
        std::cerr << "!!! CRITICAL ERROR: Failed to save shard to " << filename
                  << ". Error: " << flush_result.error().message << std::endl;
        return;
    }

    if (m_seal_callback)
    {
        m_seal_callback(filename, start_time_ms, end_time_ms);
    }

    if (!m_terminating)
    {
        std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
        m_shard_lookup_map[filename] = shard_to_save;
    }
}

void PartitionedFaissDB::cleanup_finished_futures(bool wait_for_all)
{
    if (wait_for_all)
    {
        for (auto &fut : m_sealing_futures)
        {
            if (fut.valid())
                fut.wait();
        }
    }
    m_sealing_futures.erase(std::remove_if(m_sealing_futures.begin(), m_sealing_futures.end(),
                                           [](const std::future<void> &fut) {
                                               return !fut.valid() || fut.wait_for(std::chrono::seconds(0)) ==
                                                                          std::future_status::ready;
                                           }),
                            m_sealing_futures.end());
}
