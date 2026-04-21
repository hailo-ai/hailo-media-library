#pragma once

#include "sql_factory.hpp"
#include "faiss_factory.hpp"
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <memory>
#include <shared_mutex>
#include <vector>

// Configuration for database manager initialization
struct DatabaseManagerConfig
{
    std::string sql_db_file_path; // Full path and filename for SQL database
    std::string faiss_db_path;    // Directory path for FAISS indices

    // Pre-configured FAISS factories that will be created during initialization
    struct FaissFactoryConfig
    {
        std::string name;
        int dimension;
    };

    // Pre-configured Sql factories that will be created during initialization
    struct SqlFactoryConfig
    {
        DatabaseConfig::DatabaseTable table_type;
        Database::SqliteAccessType access_type;
    };

    std::vector<SqlFactoryConfig> sql_factories;
    std::vector<FaissFactoryConfig> faiss_factories;

    DatabaseManagerConfig(const std::string &sql_file_path = "", const std::string &faiss_path = "");

    // Convenience methods to add factories
    void add_sql_factory(DatabaseConfig::DatabaseTable table_type,
                         Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);

    void add_faiss_factory(const std::string &name, int dimension);

    // Convenience method to add all common SQL factories
    void add_all_common_sql_factories();
};

class DatabaseManager
{
  public:
    // Error types for manager operations
    enum class ManagerError
    {
        NOT_INITIALIZED,
        SQL_DATABASE_ERROR,
        FAISS_INDEX_ERROR,
        INVALID_TABLE_TYPE,
        INVALID_ACCESS_TYPE,
        FACTORY_NOT_FOUND
    };

    struct ManagerErrorInfo
    {
        ManagerError type;
        std::string message;

        ManagerErrorInfo(ManagerError t, const std::string &msg);
    };

    template <typename T> using ManagerResult = tl::expected<T, ManagerErrorInfo>;

    // Singleton access
    static DatabaseManager &get_instance()
    {
        static DatabaseManager instance;
        return instance;
    }

    // Initialize the manager with configuration
    ManagerResult<bool> initialize(const DatabaseManagerConfig &config);

    // Get or create SQL database with auto-generated name
    ManagerResult<std::shared_ptr<Database>> get_or_create_sql_database(
        DatabaseConfig::DatabaseTable table_type,
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);

    // Get existing SQL database by factory name
    ManagerResult<std::shared_ptr<Database>> get_sql_database_by_name(const std::string &factory_name);

    // Get FAISS index factory by name
    ManagerResult<FaissDatabaseQuickAccess::DatabasePtr> get_faiss_index_by_name(const std::string &factory_name);

    // Get factory name for SQL database based on table type and access type
    std::string get_sql_factory_name(DatabaseConfig::DatabaseTable table_type,
                                     Database::SqliteAccessType access_type) const;

    // Get all available FAISS factory names
    std::vector<std::string> get_available_faiss_factories() const;

    // Get all SQL database factory names
    std::vector<std::string> get_sql_factory_names() const;

    // Check if manager is initialized
    bool is_initialized() const;

    // Reset the manager (for testing or reconfiguration)
    void reset();

  private:
    // Private constructor for singleton
    DatabaseManager();

    ~DatabaseManager() = default;

    // Non-copyable, non-movable
    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;
    DatabaseManager(DatabaseManager &&) = delete;
    DatabaseManager &operator=(DatabaseManager &&) = delete;

    // Structure to hold SQL database instance information
    struct SqlDatabaseInfo
    {
        DatabaseConfig::DatabaseTable table_type;
        Database::SqliteAccessType access_type;
        std::string factory_name;
    };

    // Generate standardized factory name for SQL databases
    std::string generate_sql_factory_name(DatabaseConfig::DatabaseTable table_type,
                                          Database::SqliteAccessType access_type) const;

    // Member variables
    bool m_is_initialized;
    std::string m_sql_db_file_path;
    std::string m_faiss_db_path;

    // Storage for SQL database instances
    std::unordered_map<std::string, SqlDatabaseInfo> m_sql_factory_names;

    // Storage for FAISS factory names (since they're pre-created)
    std::unordered_set<std::string> m_faiss_factory_names;

    // Thread synchronization
    mutable std::shared_mutex m_manager_mutex;
};

// Convenience wrapper for easier access
class DatabaseManagerHelper
{
  public:
    // Initialize with app custom data
    static DatabaseManager::ManagerResult<bool> initialize(const DatabaseManagerConfig &config);

    // Get factory name IDs for SQL databases (creates database if needed)
    static DatabaseManager::ManagerResult<std::string> get_faiss_table_factory_name(
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);

    static DatabaseManager::ManagerResult<std::string> get_thumbnail_table_factory_name(
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);

    static DatabaseManager::ManagerResult<std::string> get_video_table_factory_name(
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);

    // Quick access to FAISS indices
    static DatabaseManager::ManagerResult<FaissDatabaseQuickAccess::DatabasePtr> get_faiss_index_by_name(
        const std::string &factory_name);

    // Utility functions
    static std::vector<std::string> get_available_faiss_factories();

    static bool is_initialized();
};
