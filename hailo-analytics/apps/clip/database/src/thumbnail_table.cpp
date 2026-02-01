#include "thumbnail_table.hpp"
#include <iostream>
#include <algorithm>

ThumbnailTable::ThumbnailTable(const std::string &dbFile, SqliteAccessType accesstype) : Database(dbFile, accesstype)
{
}

bool ThumbnailTable::create_tables()
{
    const std::string sql = "CREATE TABLE IF NOT EXISTS thumbnails ("
                            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "  timestamp INTEGER NOT NULL UNIQUE,"
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

bool ThumbnailTable::table_exists()
{
    auto stmt = prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?;");
    if (!stmt)
        return false;

    sqlite3_bind_text(stmt, 1, "thumbnails", -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

void ThumbnailTable::insert(int64_t timestamp, const std::string &path)
{
    auto stmt = prepare("INSERT OR IGNORE INTO thumbnails (timestamp, path) VALUES (?, ?);");
    if (!stmt)
        return;
    sqlite3_bind_int64(stmt, 1, timestamp);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        // Insert failed
        std::cerr << "ThumbnailTable Insert failed: " << sqlite3_errmsg(m_db) << std::endl;
    }

    sqlite3_finalize(stmt);
}

void ThumbnailTable::insert_batch(const std::vector<std::tuple<int64_t, std::string>> &records)
{
    if (records.empty())
        return;

    // Begin transaction for better performance
    if (!execute("BEGIN TRANSACTION;"))
    {
        std::cerr << "ThumbnailTable: Failed to begin transaction for batch insert" << std::endl;
        return;
    }

    auto stmt = prepare("INSERT OR IGNORE INTO thumbnails (timestamp, path) VALUES (?, ?);");
    if (!stmt)
    {
        execute("ROLLBACK;");
        std::cerr << "ThumbnailTable: Failed to prepare statement for batch insert" << std::endl;
        return;
    }

    bool success = true;
    for (const auto &record : records)
    {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_int64(stmt, 1, std::get<0>(record));                              // timestamp
        sqlite3_bind_text(stmt, 2, std::get<1>(record).c_str(), -1, SQLITE_TRANSIENT); // path

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            std::cerr << "ThumbnailTable Batch insert failed for record (timestamp: " << std::get<0>(record)
                      << "): " << sqlite3_errmsg(m_db) << std::endl;
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

std::optional<std::string> ThumbnailTable::query_nearest(int64_t ts)
{
    auto stmt = prepare(
        "SELECT path FROM thumbnails WHERE ABS(timestamp - ?) <= 1000 ORDER BY ABS(timestamp - ?) ASC LIMIT 1;");
    if (!stmt)
        return std::nullopt;

    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_int64(stmt, 2, ts);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        std::string result = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        return result;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<ThumbnailBatchQueryResult> ThumbnailTable::query_batch_nearest(const std::vector<int64_t> &timestamps)
{
    std::vector<ThumbnailBatchQueryResult> results;

    if (timestamps.empty())
    {
        return results;
    }

    // Find the range of all query timestamps
    auto [min_ts, max_ts] = std::minmax_element(timestamps.begin(), timestamps.end());

    // Get all thumbnails in the extended range (min-1000 to max+1000)
    // This single query is much more efficient than N subqueries
    auto stmt =
        prepare("SELECT timestamp, path FROM thumbnails WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp;");
    if (!stmt)
    {
        std::cerr << "ThumbnailTable: Failed to prepare batch query statement" << std::endl;
        return results;
    }

    sqlite3_bind_int64(stmt, 1, *min_ts - 1000);
    sqlite3_bind_int64(stmt, 2, *max_ts + 1000);

    // Load all candidates into memory
    std::vector<std::pair<int64_t, std::string>> candidates;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        candidates.emplace_back(sqlite3_column_int64(stmt, 0),
                                reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
    }
    sqlite3_finalize(stmt);

    // For each query timestamp, find the nearest candidate within 1000ms
    for (int64_t query_ts : timestamps)
    {
        std::string best_path;
        int64_t best_diff = 1001; // Start with > 1000 to indicate no match

        for (const auto &[ts, path] : candidates)
        {
            int64_t diff = std::abs(ts - query_ts);
            if (diff <= 1000 && diff < best_diff)
            {
                best_diff = diff;
                best_path = path;
            }
        }

        // Only add result if we found a match within 1000ms
        if (best_diff <= 1000)
        {
            ThumbnailBatchQueryResult batch_result;
            batch_result.query_timestamp = query_ts;
            batch_result.path = best_path;
            results.push_back(batch_result);
        }
    }

    return results;
}

std::vector<ThumbnailRecord> ThumbnailTable::get_records_by_timestamp_range(int64_t start_timestamp,
                                                                            int64_t end_timestamp)
{
    std::vector<ThumbnailRecord> records;

    auto stmt = prepare(
        "SELECT id, timestamp, path FROM thumbnails WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp ASC;");
    if (!stmt)
        return records;

    sqlite3_bind_int64(stmt, 1, start_timestamp);
    sqlite3_bind_int64(stmt, 2, end_timestamp);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ThumbnailRecord record;
        record.id = sqlite3_column_int64(stmt, 0);
        record.timestamp = sqlite3_column_int64(stmt, 1);
        record.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        records.push_back(record);
    }

    sqlite3_finalize(stmt);
    return records;
}

std::vector<ThumbnailRecord> ThumbnailTable::get_records_by_timestamp_before(int64_t timestamp)
{
    // Use range query from 0
    // up to timestamp - 1 to enforce "strictly before".
    return get_records_by_timestamp_range(0, timestamp - 1);
}

bool ThumbnailTable::delete_by_id(int64_t id)
{
    auto stmt = prepare("DELETE FROM thumbnails WHERE id = ?;");
    if (!stmt)
        return false;

    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool ThumbnailTable::delete_by_ids(const std::vector<int64_t> &ids)
{
    if (ids.empty())
        return true;

    // Build dynamic SQL with placeholders
    std::string sql = "DELETE FROM thumbnails WHERE id IN (";
    for (size_t i = 0; i < ids.size(); ++i)
    {
        sql += "?";
        if (i < ids.size() - 1)
            sql += ",";
    }
    sql += ");";

    auto stmt = prepare(sql);
    if (!stmt)
        return false;

    // Bind all IDs
    for (size_t i = 0; i < ids.size(); ++i)
    {
        sqlite3_bind_int64(stmt, i + 1, ids[i]);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

int ThumbnailTable::delete_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp)
{
    auto stmt = prepare("DELETE FROM thumbnails WHERE timestamp >= ? AND timestamp <= ?;");
    if (!stmt)
        return false;

    sqlite3_bind_int64(stmt, 1, start_timestamp);
    sqlite3_bind_int64(stmt, 2, end_timestamp);
    int result = sqlite3_step(stmt);

    int changes = sqlite3_changes(sqlite3_db_handle(stmt));
    sqlite3_finalize(stmt);

    return (result == SQLITE_DONE) ? changes : -1;
}
