#include <cstddef>
#include <thread>
#include <mutex>
#include <chrono>
#include "database.hpp"
#include <iostream>

#include "common_utils.hpp"

Database::SqliteAccessType Database::get_access_type() const
{
    return m_db_access_type;
}

Database::Database(const std::string &path, SqliteAccessType accesstype) : m_db_path(path), m_db_access_type(accesstype)
{
}

Database::~Database()
{
    std::lock_guard<std::mutex> lock(m_instance_mutex);

    if (m_db)
    {
        if (m_db_access_type == SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
        {
            // Start measuring time
            auto start = std::chrono::high_resolution_clock::now();

            sqlite3_db_cacheflush(m_db); // optional: flush dirty pages from cache
            sqlite3_exec(m_db, "PRAGMA wal_checkpoint(FULL);", nullptr, nullptr, nullptr);

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            HAILO_ANALYTICS_LOG_INFO("Time taken DATABASE FLUSH AND CLOSE DB: {} ms", duration.count());
        }

        sqlite3_close(m_db);
    }
}

bool Database::open()
{

    std::lock_guard<std::mutex> lock(m_instance_mutex);

    // Make sure directory exist, if not we create directories
    FileSysUtils::ensure_directory_exists(m_db_path);

    int accesstype = SQLITE_OPEN_READWRITE;
    if (m_db_access_type == SQLITE_ACCESS_OPEN_READ_ONLY)
        accesstype = SQLITE_OPEN_READONLY;
    else if (m_db_access_type == SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
        accesstype = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    if (sqlite3_open_v2(m_db_path.c_str(), &m_db, accesstype, nullptr) != SQLITE_OK)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to open DB: {}", sqlite3_errmsg(m_db));
        return false;
    }

    // Set busy timeout
    sqlite3_busy_timeout(m_db, 5000);

    // Set PRAGMA statements hardcoded for now as all connection is suggested to have the same settings
    // for optimal performance on SD card. We can implement dynamic configuration later if needed.
    const char *pragmas = "PRAGMA journal_mode=WAL;"             // Enables WAL mode
                          "PRAGMA synchronous=OFF;"              // Best performance but low durability
                          "PRAGMA temp_store=MEMORY;"            // Keeps temp tables in memory for speed
                          "PRAGMA page_size=4096;"               // Standard page size, good for most systems
                          "PRAGMA cache_size=-16384;"            // 16MB cache (negative means KB)
                          "PRAGMA wal_autocheckpoint=1024;"      // Enable auto-checkpoint
                          "PRAGMA journal_size_limit=67108864;"; // Limit journal size to 64MB

    char *errMsg = nullptr;
    if (sqlite3_exec(m_db, pragmas, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to set WAL mode: {}", errMsg);
        sqlite3_free(errMsg);
    }

    return true;
}

bool Database::execute(const std::string &sql)
{

    char *errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        HAILO_ANALYTICS_LOG_ERROR("SQL error: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

bool Database::flush()
{

    std::lock_guard<std::mutex> lock(m_instance_mutex);

    if (m_db_access_type != SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
    {
        HAILO_ANALYTICS_LOG_WARN("Database flush is only applicable for read-write databases.");
        return false;
    }

    sqlite3_db_cacheflush(m_db); // optional: flush dirty pages from cache
    sqlite3_exec(m_db, "PRAGMA wal_checkpoint(FULL);", nullptr, nullptr, nullptr);

    return true;
}

sqlite3_stmt *Database::prepare(const std::string &sql)
{

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        HAILO_ANALYTICS_LOG_ERROR("SQL Prepare error: {}", sqlite3_errmsg(m_db));
        return nullptr;
    }

    return stmt;
}
