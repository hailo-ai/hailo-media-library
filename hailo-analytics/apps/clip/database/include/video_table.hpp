#pragma once
#include "database.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <tuple>

struct VideoInfo
{
    int64_t id; // Unique identifier for the video
    std::string path;
    int64_t start_timestamp;
    int64_t end_timestamp;
};

class VideoTable : public Database
{
  public:
    explicit VideoTable(const std::string &dbFile, SqliteAccessType accesstype = SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
    bool create_tables() override;
    bool table_exists() override;
    void insert(int64_t start, int64_t end, const std::string &path);
    void insert_batch(const std::vector<std::tuple<int64_t, int64_t, std::string>> &records);
    bool delete_by_ids(const std::vector<int64_t> &ids);
    bool delete_by_id(int64_t id);
    int delete_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp);
    std::vector<VideoInfo> get_oldest_videos(float percentage);
    std::vector<VideoInfo> query_covering(int64_t timestamp, int64_t tolerance = 7500);
    std::vector<VideoInfo> get_records_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp);
};
