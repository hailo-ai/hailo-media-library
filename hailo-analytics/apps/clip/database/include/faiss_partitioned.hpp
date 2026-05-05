#pragma once

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "faiss_shard.hpp"
#include <faiss/IndexShards.h>
#include <future>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <algorithm>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

// Sealing hot shard is either reaching specified hot_shard_max_size or exceeding HOT_SHARD_MAX_ALLOWED_DURATION_SEC
// NOTE: Recommend not to set HOT_SHARD_MAX_ALLOWED_DURATION_SEC too high to avoid long-lived hot shards
#define HOT_SHARD_MAX_ALLOWED_DURATION_SEC (120.0f)

class TaskFutureRunner
{
  public:
    TaskFutureRunner();

    ~TaskFutureRunner();

    template <typename F, typename... Args>
    auto enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using RetType = typename std::result_of<F(Args...)>::type;

        auto task =
            std::make_shared<std::packaged_task<RetType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<RetType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_cv.notify_one();
        return res;
    }

  private:
    void run();

    std::thread m_worker;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    bool m_stop;
};

class PartitionedFaissDB
{
  public:
    using SealCallback = std::function<void(const std::string &, int64_t, int64_t)>;
    using ResultShardPtr = FaissShard::Result<std::shared_ptr<FaissShard>>;

    struct GetResult
    {
        std::vector<float> vector;
        std::string source_file;
    };

    template <typename T> using Result = FaissShard::Result<T>;
    using Error = FaissShard::Error;
    using ErrorType = FaissShard::ErrorType;
    using SearchResult = FaissShard::SearchResult;

    static Result<std::unique_ptr<PartitionedFaissDB>> create(int dimension, const std::string &db_directory,
                                                              const std::string &file_prefix,
                                                              const std::vector<std::string> &initial_cold_files,
                                                              bool auto_shard_seal, size_t hot_shard_max_size);

    ~PartitionedFaissDB();

    Result<faiss::idx_t> insert(const std::vector<float> &vector);

    Result<std::vector<SearchResult>> search(const std::vector<float> &query, int k = 5) const;

    Result<GetResult> get_vector_by_id(faiss::idx_t id) const;

    Result<std::vector<faiss::idx_t>> get_all_ids_from_shard(const std::string &filename) const;

    bool exists(faiss::idx_t id) const;

    size_t size() const;

    bool flush();

    bool remove_partition(const std::string &filename);

    void register_seal_callback(SealCallback callback);

    // Generate random embeddings with specified size, will only generate
    // if the index does not already have more than the specified number of vectors.
    Result<bool> generate_random_embeddings(size_t num_vectors);

  private:
    PartitionedFaissDB(int dimension, const std::string &db_dir, const std::string &prefix, bool auto_seal,
                       size_t max_size);

    Result<void> insert(const std::vector<float> &vector, faiss::idx_t id);

    void seal_hot_shard();

    void create_new_hot_shard();

    void rebuild_search_index();

    void save_cold_shard(std::shared_ptr<FaissShard> shard_to_save, int64_t start_time_ms, int64_t end_time_ms);

    void cleanup_finished_futures(bool wait_for_all);

    std::shared_ptr<FaissShard> m_hot_shard;
    std::vector<std::shared_ptr<FaissShard>> m_cold_shards;
    std::unordered_map<std::string, std::shared_ptr<FaissShard>> m_shard_lookup_map;
    std::shared_ptr<faiss::IndexShards> m_search_index;

    bool m_terminating = false;
    std::atomic<faiss::idx_t> m_next_auto_id;
    std::unordered_set<faiss::idx_t> m_global_used_ids;
    int m_dimension;
    std::string m_db_directory;
    std::string m_file_prefix;
    bool m_auto_shard_seal;
    size_t m_hot_shard_max_size;
    SealCallback m_seal_callback;
    std::chrono::system_clock::time_point m_hot_shard_start_time;
    mutable std::shared_mutex m_rw_mutex;
    std::vector<std::future<void>> m_sealing_futures;
    TaskFutureRunner m_sealing_worker;
};
