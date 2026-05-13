#include "faiss_table.hpp"
#include <iostream>
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

FaissTable::FaissTable(const std::string &dbFile, SqliteAccessType accesstype) : Database(dbFile, accesstype)
{
}

bool FaissTable::create_tables()
{
    const std::string sql = "CREATE TABLE IF NOT EXISTS faiss_metadata ("
                            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "  faiss_id INTEGER NOT NULL,"
                            "  track_id INTEGER NOT NULL,"
                            "  timestamp INTEGER NOT NULL,"
                            "  network_embedding_name TEXT NOT NULL,"
                            "  classification_label TEXT,"
                            "  UNIQUE(faiss_id, network_embedding_name));";

    return execute(sql);
}

bool FaissTable::table_exists()
{
    auto stmt = prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?;");
    if (!stmt)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, "faiss_metadata", -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

void FaissTable::insert(int64_t faissId, int32_t trackId, int64_t timestamp, const std::string &embedding_name,
                        const std::string &classification_label)
{
    auto stmt = prepare("INSERT OR IGNORE INTO faiss_metadata (faiss_id, track_id, timestamp, network_embedding_name, "
                        "classification_label) "
                        "VALUES (?, ?, ?, ?, ?);");
    if (!stmt)
        return;
    sqlite3_bind_int64(stmt, 1, faissId);
    sqlite3_bind_int(stmt, 2, trackId);
    sqlite3_bind_int64(stmt, 3, timestamp);
    sqlite3_bind_text(stmt, 4, embedding_name.c_str(), -1, SQLITE_TRANSIENT);
    if (!classification_label.empty())
    {
        sqlite3_bind_text(stmt, 5, classification_label.c_str(), -1, SQLITE_TRANSIENT);
    }
    else
    {
        sqlite3_bind_null(stmt, 5);
    }
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        // Insert failed
        HAILO_ANALYTICS_LOG_ERROR("FaissTable Insert failed: {}", sqlite3_errmsg(m_db));
    }

    sqlite3_finalize(stmt);
}

void FaissTable::insert_batch(
    const std::vector<std::tuple<int64_t, int32_t, int64_t, std::string, std::string>> &records)
{
    if (records.empty())
        return;

    // Begin transaction for better performance
    if (!execute("BEGIN TRANSACTION;"))
    {
        HAILO_ANALYTICS_LOG_ERROR("FaissTable: Failed to begin transaction for batch insert");
        return;
    }

    auto stmt = prepare("INSERT OR IGNORE INTO faiss_metadata (faiss_id, track_id, timestamp, network_embedding_name, "
                        "classification_label) "
                        "VALUES (?, ?, ?, ?, ?);");
    if (!stmt)
    {
        execute("ROLLBACK;");
        HAILO_ANALYTICS_LOG_ERROR("FaissTable: Failed to prepare statement for batch insert");
        return;
    }

    bool success = true;
    for (const auto &record : records)
    {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_int64(stmt, 1, std::get<0>(record));                              // faiss_id
        sqlite3_bind_int(stmt, 2, std::get<1>(record));                                // track_id
        sqlite3_bind_int64(stmt, 3, std::get<2>(record));                              // timestamp
        sqlite3_bind_text(stmt, 4, std::get<3>(record).c_str(), -1, SQLITE_TRANSIENT); // embedding_name

        const std::string &classification_label = std::get<4>(record);
        if (!classification_label.empty())
        {
            sqlite3_bind_text(stmt, 5, classification_label.c_str(), -1, SQLITE_TRANSIENT);
        }
        else
        {
            sqlite3_bind_null(stmt, 5);
        }

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            HAILO_ANALYTICS_LOG_ERROR("FaissTable Batch insert failed for record (faiss_id: {}, track_id: {}): {}",
                                      std::get<0>(record), std::get<1>(record), sqlite3_errmsg(m_db));
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

std::optional<FaissTableQueryResult> FaissTable::query_timestamp(int64_t faissId, const std::string &embedding_name)
{
    auto stmt = prepare("SELECT track_id, timestamp, classification_label FROM faiss_metadata WHERE faiss_id = ? AND "
                        "network_embedding_name = ?;");
    if (!stmt)
        return std::nullopt;
    sqlite3_bind_int64(stmt, 1, faissId);
    sqlite3_bind_text(stmt, 2, embedding_name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        FaissTableQueryResult result;
        result.track_id = sqlite3_column_int(stmt, 0);
        result.timestamp = sqlite3_column_int64(stmt, 1);

        const char *label_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        result.classification_label = label_ptr ? label_ptr : "";

        sqlite3_finalize(stmt);
        return result;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<FaissTableBatchQueryResult> FaissTable::query_batch_timestamp(
    const std::vector<std::pair<int64_t, std::string>> &queries)
{
    std::vector<FaissTableBatchQueryResult> results;

    if (queries.empty())
    {
        return results;
    }

    // Process queries in batches to avoid expression tree depth limit
    const size_t MAX_BATCH_SIZE = 100; // Limit OR conditions to avoid depth issues

    for (size_t offset = 0; offset < queries.size(); offset += MAX_BATCH_SIZE)
    {
        size_t batch_size = std::min(MAX_BATCH_SIZE, queries.size() - offset);

        // Build a query with multiple conditions using OR clauses
        std::string sql = "SELECT faiss_id, track_id, timestamp, network_embedding_name, classification_label FROM "
                          "faiss_metadata WHERE ";

        // Add conditions for each query in this batch
        for (size_t i = 0; i < batch_size; ++i)
        {
            if (i > 0)
                sql += " OR ";
            sql += "(faiss_id = ? AND network_embedding_name = ?)";
        }
        sql += ";";

        auto stmt = prepare(sql.c_str());
        if (!stmt)
        {
            HAILO_ANALYTICS_LOG_ERROR("FaissTable: Failed to prepare batch query statement");
            return results;
        }

        // Bind parameters for each query condition in this batch
        int param_index = 1;
        for (size_t i = 0; i < batch_size; ++i)
        {
            const auto &query = queries[offset + i];
            sqlite3_bind_int64(stmt, param_index++, query.first);                               // faiss_id
            sqlite3_bind_text(stmt, param_index++, query.second.c_str(), -1, SQLITE_TRANSIENT); // embedding_name
        }

        // Execute and collect results
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int64_t faiss_id = sqlite3_column_int64(stmt, 0);
            int32_t track_id = sqlite3_column_int(stmt, 1);
            int64_t timestamp = sqlite3_column_int64(stmt, 2);
            const char *name_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            std::string embedding_name = name_ptr ? name_ptr : "";

            const char *label_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            std::string classification_label = label_ptr ? label_ptr : "";

            FaissTableBatchQueryResult batch_result;
            batch_result.faiss_id = faiss_id;
            batch_result.embedding_name = embedding_name;
            batch_result.result.track_id = track_id;
            batch_result.result.timestamp = timestamp;
            batch_result.result.classification_label = classification_label;

            results.push_back(batch_result);
        }

        sqlite3_finalize(stmt);
    }

    return results;
}

std::vector<FaissRecord> FaissTable::get_records_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp)
{
    std::vector<FaissRecord> records;
    auto stmt = prepare("SELECT id, faiss_id, track_id, timestamp, network_embedding_name, classification_label FROM "
                        "faiss_metadata WHERE "
                        "timestamp >= ? AND timestamp <= ? ORDER BY timestamp;");
    if (!stmt)
        return records;

    sqlite3_bind_int64(stmt, 1, start_timestamp);
    sqlite3_bind_int64(stmt, 2, end_timestamp);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        FaissRecord record;
        record.id = sqlite3_column_int64(stmt, 0);
        record.faiss_id = sqlite3_column_int64(stmt, 1);
        record.track_id = sqlite3_column_int(stmt, 2);
        record.timestamp = sqlite3_column_int64(stmt, 3);
        const char *emb_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        record.network_embedding_name = emb_ptr ? emb_ptr : "";

        const char *cls_ptr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
        record.classification_label = cls_ptr ? cls_ptr : "";

        records.push_back(record);
    }

    sqlite3_finalize(stmt);
    return records;
}

bool FaissTable::delete_by_id(int64_t id)
{
    auto stmt = prepare("DELETE FROM faiss_metadata WHERE id = ?;");
    if (!stmt)
        return false;

    sqlite3_bind_int64(stmt, 1, id);
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool FaissTable::delete_by_faiss_id(int64_t faiss_id, const std::string &embedding_name)
{
    auto stmt = prepare("DELETE FROM faiss_metadata WHERE faiss_id = ? AND network_embedding_name = ?;");
    if (!stmt)
        return false;

    sqlite3_bind_int64(stmt, 1, faiss_id);
    sqlite3_bind_text(stmt, 2, embedding_name.c_str(), -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool FaissTable::delete_batch_by_ids(const std::vector<int64_t> &ids)
{
    if (ids.empty())
        return true;

    // Build placeholders for the IN clause
    std::string placeholders = "?";
    for (size_t i = 1; i < ids.size(); ++i)
    {
        placeholders += ",?";
    }

    std::string sql = "DELETE FROM faiss_metadata WHERE id IN (" + placeholders + ");";
    auto stmt = prepare(sql.c_str());
    if (!stmt)
        return false;

    // Bind all the IDs
    for (size_t i = 0; i < ids.size(); ++i)
    {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), ids[i]);
    }

    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool FaissTable::delete_batch_by_faiss_ids(const std::vector<std::pair<int64_t, std::string>> &faiss_ids)
{
    if (faiss_ids.empty())
        return true;

    // Use a transaction for better performance
    execute("BEGIN TRANSACTION;");

    auto stmt = prepare("DELETE FROM faiss_metadata WHERE faiss_id = ? AND network_embedding_name = ?;");
    if (!stmt)
    {
        execute("ROLLBACK;");
        return false;
    }

    bool success = true;
    for (const auto &pair : faiss_ids)
    {
        sqlite3_bind_int64(stmt, 1, pair.first);
        sqlite3_bind_text(stmt, 2, pair.second.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            success = false;
            break;
        }
        sqlite3_reset(stmt);
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

    return success;
}

int FaissTable::delete_by_timestamp_range(int64_t start_timestamp, int64_t end_timestamp)
{
    auto stmt = prepare("DELETE FROM faiss_metadata WHERE timestamp >= ? AND timestamp <= ?;");
    if (!stmt)
        return -1;

    sqlite3_bind_int64(stmt, 1, start_timestamp);
    sqlite3_bind_int64(stmt, 2, end_timestamp);
    int result = sqlite3_step(stmt);

    int changes = sqlite3_changes(sqlite3_db_handle(stmt));
    sqlite3_finalize(stmt);

    return (result == SQLITE_DONE) ? changes : -1;
}
