#pragma once
#include <string>
#include <sqlite3.h>
#include <mutex>

class Database
{

  public:
    enum SqliteAccessType
    {
        SQLITE_ACCESS_OPEN_CREATE_READ_WRITE,
        SQLITE_ACCESS_OPEN_READ_ONLY
    };

  protected:
    sqlite3 *m_db = nullptr;
    std::string m_db_path;
    SqliteAccessType m_db_access_type;
    std::mutex m_instance_mutex;

  public:
    explicit Database(const std::string &path, SqliteAccessType accesstype = SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
    virtual ~Database();

    SqliteAccessType get_access_type() const;
    bool open();
    bool execute(const std::string &sql);
    bool flush();
    sqlite3_stmt *prepare(const std::string &sql);
    virtual bool create_tables() = 0;
    virtual bool table_exists() = 0;
};
