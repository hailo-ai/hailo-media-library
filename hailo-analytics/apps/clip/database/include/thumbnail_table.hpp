#pragma once
#include "database.hpp"
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <cstdint>

struct ThumbnailRecord
{
    int64_t id;
    int64_t timestamp;
    std::string path;
};

struct ThumbnailBatchQueryResult
{
    int64_t query_timestamp;
    std::string path;
};

class ThumbnailTable : public Database
{
  public:
    explicit ThumbnailTable(const std::string &dbFile,
                            SqliteAccessType accesstype = SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
    bool create_tables() override;
    bool table_exists() override;
    void insert(int64_t timestamp, const std::string &path);
    void insert_batch(const std::vector<std::tuple<int64_t, std::string>> &records);
    std::optional<std::string> query_nearest(int64_t timestamp);
    std::vector<ThumbnailBatchQueryResult> query_batch_nearest(const std::vector<int64_t> &timestamps);
    std::vector<ThumbnailRecord> get_records_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp);
    std::vector<ThumbnailRecord> get_records_by_timestamp_before(int64_t timestamp);
    bool delete_by_id(int64_t id);
    bool delete_by_ids(const std::vector<int64_t> &ids);
    int delete_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp);
};
