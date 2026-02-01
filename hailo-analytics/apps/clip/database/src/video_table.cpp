#include "video_table.hpp"
#include <iostream>
#include <cstdint>

VideoTable::VideoTable(const std::string &dbFile, SqliteAccessType accesstype) : Database(dbFile, accesstype)
{
}

bool VideoTable::create_tables()
{
    const std::string sql = "CREATE TABLE IF NOT EXISTS videos ("
                            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "  start_timestamp INTEGER NOT NULL,"
                            "  end_timestamp INTEGER NOT NULL,"
                            "  path TEXT NOT NULL);";

    bool success = execute(sql);
    if (success)
    {
        // Force a checkpoint to make schema changes durable and visible to other connections.
        // TRUNCATE is often a good choice here as it commits and shrinks the WAL file
        execute("PRAGMA wal_checkpoint(TRUNCATE);");
    }

    return success;
}

bool VideoTable::table_exists()
{
    auto stmt = prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?;");
    if (!stmt)
        return false;

    sqlite3_bind_text(stmt, 1, "videos", -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

void VideoTable::insert(int64_t start, int64_t end, const std::string &path)
{
    auto stmt = prepare("INSERT INTO videos (start_timestamp, end_timestamp, path) VALUES (?, ?, ?);");
    if (!stmt)
        return;
    sqlite3_bind_int64(stmt, 1, start);
    sqlite3_bind_int64(stmt, 2, end);
    sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        // Insert failed
        std::cerr << "VideoTable Insert failed: " << sqlite3_errmsg(m_db) << std::endl;
    }

    sqlite3_finalize(stmt);
}

void VideoTable::insert_batch(const std::vector<std::tuple<int64_t, int64_t, std::string>> &records)
{
    if (records.empty())
        return;

    // Begin transaction for better performance
    if (!execute("BEGIN TRANSACTION;"))
    {
        std::cerr << "VideoTable: Failed to begin transaction for batch insert" << std::endl;
        return;
    }

    auto stmt = prepare("INSERT INTO videos (start_timestamp, end_timestamp, path) VALUES (?, ?, ?);");
    if (!stmt)
    {
        execute("ROLLBACK;");
        std::cerr << "VideoTable: Failed to prepare statement for batch insert" << std::endl;
        return;
    }

    bool success = true;
    for (const auto &record : records)
    {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_int64(stmt, 1, std::get<0>(record));                              // start_timestamp
        sqlite3_bind_int64(stmt, 2, std::get<1>(record));                              // end_timestamp
        sqlite3_bind_text(stmt, 3, std::get<2>(record).c_str(), -1, SQLITE_TRANSIENT); // path

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            std::cerr << "VideoTable Batch insert failed for record (start: " << std::get<0>(record)
                      << ", end: " << std::get<1>(record) << "): " << sqlite3_errmsg(m_db) << std::endl;
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (success)
    {
        execute("COMMIT;");
    }
    else
    {
        execute("ROLLBACK;");
    }
}

std::vector<VideoInfo> VideoTable::query_covering(int64_t ts, int64_t tolerance)
{
    std::vector<VideoInfo> results;
    auto stmt = prepare("SELECT id, path, start_timestamp, end_timestamp FROM videos WHERE start_timestamp <= (? + ?) "
                        "AND end_timestamp >= (? - ?);");
    if (!stmt)
        return results;
    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_int64(stmt, 2, tolerance);
    sqlite3_bind_int64(stmt, 3, ts);
    sqlite3_bind_int64(stmt, 4, tolerance);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        VideoInfo info;
        info.id = sqlite3_column_int64(stmt, 0);
        info.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        info.start_timestamp = sqlite3_column_int64(stmt, 2);
        info.end_timestamp = sqlite3_column_int64(stmt, 3);
        results.emplace_back(std::move(info));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<VideoInfo> VideoTable::get_oldest_videos(float percentage)
{
    std::vector<VideoInfo> results;

    // First get total count
    auto countStmt = prepare("SELECT COUNT(*) FROM videos;");
    if (!countStmt)
        return results;

    int totalCount = 0;
    if (sqlite3_step(countStmt) == SQLITE_ROW)
    {
        totalCount = sqlite3_column_int(countStmt, 0);
    }
    sqlite3_finalize(countStmt);

    if (totalCount == 0)
        return results;

    // Calculate limit (ensure at least 1 if percentage > 0)
    int limit = std::max(1, static_cast<int>(totalCount * percentage / 100.0));

    // Get oldest videos
    auto stmt =
        prepare("SELECT id, path, start_timestamp, end_timestamp FROM videos ORDER BY start_timestamp ASC LIMIT ?;");
    if (!stmt)
        return results;

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        VideoInfo info;
        info.id = sqlite3_column_int64(stmt, 0);
        info.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        info.start_timestamp = sqlite3_column_int64(stmt, 2);
        info.end_timestamp = sqlite3_column_int64(stmt, 3);
        results.emplace_back(std::move(info));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool VideoTable::delete_by_ids(const std::vector<int64_t> &ids)
{
    if (ids.empty())
        return true;

    // Create placeholders for the IN clause
    std::string placeholders = "?";
    for (size_t i = 1; i < ids.size(); ++i)
    {
        placeholders += ",?";
    }

    std::string sql = "DELETE FROM videos WHERE id IN (" + placeholders + ");";
    auto stmt = prepare(sql);
    if (!stmt)
        return false;

    // Bind all IDs
    for (size_t i = 0; i < ids.size(); ++i)
    {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), ids[i]);
    }

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool VideoTable::delete_by_id(int64_t id)
{
    auto stmt = prepare("DELETE FROM videos WHERE id = ?;");
    if (!stmt)
        return false;

    sqlite3_bind_int64(stmt, 1, id);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

int VideoTable::delete_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp)
{
    auto stmt = prepare("DELETE FROM videos WHERE end_timestamp >= ? AND end_timestamp <= ?;");
    if (!stmt)
        return false;

    sqlite3_bind_int64(stmt, 1, start_timestamp);
    sqlite3_bind_int64(stmt, 2, end_timestamp);
    int result = sqlite3_step(stmt);

    int changes = sqlite3_changes(sqlite3_db_handle(stmt));
    sqlite3_finalize(stmt);

    return (result == SQLITE_DONE) ? changes : -1;
}

std::vector<VideoInfo> VideoTable::get_records_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp)
{
    std::vector<VideoInfo> results;

    auto stmt = prepare("SELECT id, path, start_timestamp, end_timestamp FROM videos WHERE end_timestamp >= ? AND "
                        "end_timestamp <= ? ORDER BY end_timestamp ASC;");
    if (!stmt)
        return results;

    sqlite3_bind_int64(stmt, 1, start_timestamp);
    sqlite3_bind_int64(stmt, 2, end_timestamp);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        VideoInfo info;
        info.id = sqlite3_column_int64(stmt, 0);
        info.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        info.start_timestamp = sqlite3_column_int64(stmt, 2);
        info.end_timestamp = sqlite3_column_int64(stmt, 3);
        results.emplace_back(std::move(info));
    }

    sqlite3_finalize(stmt);
    return results;
}
