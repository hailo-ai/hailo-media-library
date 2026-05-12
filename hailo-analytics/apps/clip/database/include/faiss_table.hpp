#pragma once
#include "database.hpp"
#include <optional>
#include <vector>
#include <string>
#include <tuple>
#include <cstdint>
#include <utility>

struct FaissTableQueryResult
{
    int32_t track_id;
    int64_t timestamp;
    std::string classification_label;
};

struct FaissRecord
{
    int64_t id;
    int64_t faiss_id;
    int32_t track_id;
    int64_t timestamp;
    std::string network_embedding_name;
    std::string classification_label;
};

struct FaissTableBatchQueryResult
{
    int64_t faiss_id;
    std::string embedding_name;
    FaissTableQueryResult result;
};

class FaissTable : public Database
{
  public:
    explicit FaissTable(const std::string &dbFile, SqliteAccessType accesstype = SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
    bool create_tables() override;
    bool table_exists() override;
    void insert(int64_t faissId, int32_t trackId, int64_t timestamp, const std::string &embedding_name,
                const std::string &classification_label = "");
    void insert_batch(const std::vector<std::tuple<int64_t, int32_t, int64_t, std::string, std::string>> &records);
    std::optional<FaissTableQueryResult> query_timestamp(int64_t faissId, const std::string &embedding_name);
    std::vector<FaissTableBatchQueryResult> query_batch_timestamp(
        const std::vector<std::pair<int64_t, std::string>> &queries);
    std::vector<FaissRecord> get_records_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp);

    // Delete functions
    bool delete_by_id(int64_t id);
    bool delete_by_faiss_id(int64_t faiss_id, const std::string &embedding_name);
    bool delete_batch_by_ids(const std::vector<int64_t> &ids);
    bool delete_batch_by_faiss_ids(const std::vector<std::pair<int64_t, std::string>> &faiss_ids);
    int delete_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp);
};
