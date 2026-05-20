#pragma once

#include <faiss/IndexFlat.h>
#include <faiss/IndexIDMap.h>
#include <faiss/MetricType.h>
#include <faiss/index_io.h>
#include <tl/expected.hpp>
#include <vector>
#include <unordered_set>
#include <memory>
#include <string>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <chrono>
#include <iomanip>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "common_utils.hpp"

namespace fs = std::filesystem;

class FaissShard
{
  public:
    enum class ErrorType
    {
        INVALID_DIMENSION,
        DIMENSION_MISMATCH,
        ID_ALREADY_EXISTS,
        ID_NOT_FOUND,
        VECTOR_SIZE_MISMATCH,
        EMPTY_INDEX,
        INVALID_K,
        FILE_NOT_FOUND,
        FILE_LOAD_ERROR,
        FILE_SAVE_ERROR,
        NO_FILE_PATH,
        INVALID_INDEX_TYPE,
        DIMENSION_MISMATCH_LOADED,
        FAISS_ERROR
    };

    struct Error
    {
        ErrorType type;
        std::string message;
        Error(ErrorType t, const std::string &msg);
    };

    template <typename T> using Result = tl::expected<T, Error>;

    struct SearchResult
    {
        faiss::idx_t id;
        float distance;
        SearchResult(faiss::idx_t id, float distance);
    };

    struct Statistics
    {
        size_t total_vectors;
        int dimension;
        size_t memory_usage_bytes;
        std::string file_path;
        bool latency_stats_enabled;
        double avg_insert_latency_ms, avg_search_latency_ms;
        size_t total_insert_operations, total_search_operations;
        double min_insert_latency_ms, max_insert_latency_ms;
        double min_search_latency_ms, max_search_latency_ms;
    };

    static Result<std::shared_ptr<FaissShard>> create(int dimension, const std::string &index_file_path = "");

    ~FaissShard() = default;
    FaissShard(const FaissShard &) = delete;
    FaissShard &operator=(const FaissShard &) = delete;
    FaissShard(FaissShard &&) = delete;
    FaissShard &operator=(FaissShard &&) = delete;

    Result<void> insert(const std::vector<float> &vector, faiss::idx_t id);
    Result<std::vector<SearchResult>> search(const std::vector<float> &query, int k = 5) const;
    Result<void> flush();
    Result<std::vector<float>> get_vector_by_id(faiss::idx_t id) const;
    bool exists(faiss::idx_t id) const;
    size_t size() const;
    std::vector<faiss::idx_t> get_all_ids() const;

    void set_file_path(const std::string &path);
    const std::string &get_file_path() const;
    faiss::Index *get_index();
    const faiss::Index *get_index() const;

  private:
    FaissShard(int dimension, const std::string &index_file_path);

    void create_new_index();
    Result<void> rebuild_id_tracking();
    Result<void> load_from_file();

    std::unique_ptr<faiss::IndexIDMap2> m_index;
    int m_dimension;
    std::unordered_set<faiss::idx_t> m_used_ids;
    std::string m_index_file_path;
    mutable std::shared_mutex m_rw_mutex;
};
