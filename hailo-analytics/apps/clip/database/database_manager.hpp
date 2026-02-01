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

    DatabaseManagerConfig(const std::string &sql_file_path = "", const std::string &faiss_path = "")
        : sql_db_file_path(sql_file_path), faiss_db_path(faiss_path)
    {
    }

    // Convenience methods to add factories
    void add_sql_factory(DatabaseConfig::DatabaseTable table_type,
                         Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
    {
        sql_factories.push_back({table_type, access_type});
    }

    void add_faiss_factory(const std::string &name, int dimension)
    {
        faiss_factories.push_back({name, dimension});
    }

    // Convenience method to add all common SQL factories
    void add_all_common_sql_factories()
    {
        // Read-write versions
        add_sql_factory(DatabaseConfig::FAISS_TABLE, Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
        add_sql_factory(DatabaseConfig::THUMBNAIL_TABLE, Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
        add_sql_factory(DatabaseConfig::VIDEO_TABLE, Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);

        // Read-only versions
        add_sql_factory(DatabaseConfig::FAISS_TABLE, Database::SQLITE_ACCESS_OPEN_READ_ONLY);
        add_sql_factory(DatabaseConfig::THUMBNAIL_TABLE, Database::SQLITE_ACCESS_OPEN_READ_ONLY);
        add_sql_factory(DatabaseConfig::VIDEO_TABLE, Database::SQLITE_ACCESS_OPEN_READ_ONLY);
    }
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

        ManagerErrorInfo(ManagerError t, const std::string &msg) : type(t), message(msg)
        {
        }
    };

    template <typename T> using ManagerResult = tl::expected<T, ManagerErrorInfo>;

    // Singleton access
    static DatabaseManager &get_instance()
    {
        static DatabaseManager instance;
        return instance;
    }

    // Initialize the manager with configuration
    ManagerResult<bool> initialize(const DatabaseManagerConfig &config)
    {
        std::unique_lock<std::shared_mutex> lock(m_manager_mutex);

        if (m_is_initialized)
        {
            return tl::unexpected(
                ManagerErrorInfo(ManagerError::NOT_INITIALIZED, "DatabaseManager is already initialized"));
        }

        m_sql_db_file_path = config.sql_db_file_path;
        m_faiss_db_path = config.faiss_db_path;

        // Pre-create all SQL databases
        for (const auto &sql_config : config.sql_factories)
        {
            std::string factory_name = generate_sql_factory_name(sql_config.table_type, sql_config.access_type);

            // Create database configuration
            DatabaseConfig db_config(sql_config.table_type, m_sql_db_file_path, sql_config.access_type);

            // Create or get database from factory
            auto result = SqlDatabaseQuickAccess::get_or_create_database(factory_name, db_config);
            if (!result)
            {
                return tl::unexpected(ManagerErrorInfo(ManagerError::SQL_DATABASE_ERROR,
                                                       "Failed to create SQL database '" + factory_name +
                                                           "': " + result.error().message));
            }

            // Store the instance information
            SqlDatabaseInfo info;
            info.table_type = sql_config.table_type;
            info.access_type = sql_config.access_type;
            info.factory_name = factory_name;

            m_sql_factory_names[factory_name] = info;
        }

        // Pre-create all FAISS factories
        for (const auto &faiss_config : config.faiss_factories)
        {

            auto faiss_cold_shard_files = FileSysUtils::get_all_file_names(m_faiss_db_path, true, faiss_config.name);
            FaissDatabaseConfig faiss_param_config(faiss_config.dimension, m_faiss_db_path, faiss_config.name,
                                                   faiss_cold_shard_files,
                                                   true, // auto_flush
                                                   1000  // flush_threshold
            );

            auto result = FaissDatabaseQuickAccess::get_or_create_database(faiss_config.name, faiss_param_config);

            if (!result)
            {
                return tl::unexpected(ManagerErrorInfo(ManagerError::FAISS_INDEX_ERROR,
                                                       "Failed to create FAISS factory '" + faiss_config.name +
                                                           "': " + result.error().message));
            }

            m_faiss_factory_names.insert(faiss_config.name);
        }

        m_is_initialized = true;
        return true;
    }

    // Get or create SQL database with auto-generated name
    ManagerResult<std::shared_ptr<Database>> get_or_create_sql_database(
        DatabaseConfig::DatabaseTable table_type,
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
    {

        // Thread-safe insertion (upgrade to unique lock)
        std::unique_lock<std::shared_mutex> unique_lock(m_manager_mutex);

        if (!m_is_initialized)
        {
            return tl::unexpected(ManagerErrorInfo(ManagerError::NOT_INITIALIZED, "DatabaseManager not initialized"));
        }

        // Generate factory name based on table type and access type
        std::string factory_name = generate_sql_factory_name(table_type, access_type);

        // Check if we already have this database instance
        auto existing_it = m_sql_factory_names.find(factory_name);
        if (existing_it != m_sql_factory_names.end())
        {
            return SqlDatabaseQuickAccess::get_database(existing_it->first).value();
        }

        // Create new database configuration
        DatabaseConfig config(table_type, m_sql_db_file_path, access_type);

        // Create or get database from factory
        auto result = SqlDatabaseQuickAccess::get_or_create_database(factory_name, config);
        if (!result)
        {
            return tl::unexpected(ManagerErrorInfo(ManagerError::SQL_DATABASE_ERROR,
                                                   "Failed to create SQL database: " + result.error().message));
        }

        // Store the instance information
        SqlDatabaseInfo info;
        info.table_type = table_type;
        info.access_type = access_type;
        info.factory_name = factory_name;

        m_sql_factory_names[factory_name] = info;

        return result.value();
    }

    // Get existing SQL database by factory name
    ManagerResult<std::shared_ptr<Database>> get_sql_database_by_name(const std::string &factory_name)
    {
        std::shared_lock<std::shared_mutex> lock(m_manager_mutex);

        if (!m_is_initialized)
        {
            return tl::unexpected(ManagerErrorInfo(ManagerError::NOT_INITIALIZED, "DatabaseManager not initialized"));
        }

        auto it = m_sql_factory_names.find(factory_name);
        if (it == m_sql_factory_names.end())
        {
            return tl::unexpected(ManagerErrorInfo(ManagerError::FACTORY_NOT_FOUND,
                                                   "SQL database factory '" + factory_name + "' not found"));
        }

        return SqlDatabaseQuickAccess::get_database(it->first).value();
    }

    // Get FAISS index factory by name
    ManagerResult<FaissDatabaseQuickAccess::DatabasePtr> get_faiss_index_by_name(const std::string &factory_name)
    {
        std::shared_lock<std::shared_mutex> lock(m_manager_mutex);

        if (!m_is_initialized)
        {
            return tl::unexpected(ManagerErrorInfo(ManagerError::NOT_INITIALIZED, "DatabaseManager not initialized"));
        }

        if (m_faiss_factory_names.find(factory_name) == m_faiss_factory_names.end())
        {
            return tl::unexpected(
                ManagerErrorInfo(ManagerError::FACTORY_NOT_FOUND, "FAISS factory '" + factory_name + "' not found"));
        }

        auto result = FaissDatabaseQuickAccess::get_database(factory_name);
        if (!result)
        {
            return tl::unexpected(ManagerErrorInfo(ManagerError::FAISS_INDEX_ERROR,
                                                   "Failed to get FAISS factory: " + result.error().message));
        }

        return result.value();
    }

    // Get factory name for SQL database based on table type and access type
    std::string get_sql_factory_name(DatabaseConfig::DatabaseTable table_type,
                                     Database::SqliteAccessType access_type) const
    {
        return generate_sql_factory_name(table_type, access_type);
    }

    // Get all available FAISS factory names
    std::vector<std::string> get_available_faiss_factories() const
    {
        std::shared_lock<std::shared_mutex> lock(m_manager_mutex);

        std::vector<std::string> names;
        names.reserve(m_faiss_factory_names.size());

        for (const auto &name : m_faiss_factory_names)
        {
            names.push_back(name);
        }

        return names;
    }

    // Get all SQL database factory names
    std::vector<std::string> get_sql_factory_names() const
    {
        std::shared_lock<std::shared_mutex> lock(m_manager_mutex);

        std::vector<std::string> names;
        names.reserve(m_sql_factory_names.size());

        for (const auto &pair : m_sql_factory_names)
        {
            names.push_back(pair.first);
        }

        return names;
    }

    // Check if manager is initialized
    bool is_initialized() const
    {
        std::shared_lock<std::shared_mutex> lock(m_manager_mutex);
        return m_is_initialized;
    }

    // Reset the manager (for testing or reconfiguration)
    void reset()
    {
        std::unique_lock<std::shared_mutex> lock(m_manager_mutex);

        m_sql_factory_names.clear();
        m_faiss_factory_names.clear();
        m_sql_db_file_path.clear();
        m_faiss_db_path.clear();
        m_is_initialized = false;
    }

  private:
    // Private constructor for singleton
    DatabaseManager() : m_is_initialized(false)
    {
    }

    ~DatabaseManager()
    {
    }

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
                                          Database::SqliteAccessType access_type) const
    {
        std::string table_name;
        switch (table_type)
        {
        case DatabaseConfig::FAISS_TABLE:
            table_name = "faiss";
            break;
        case DatabaseConfig::THUMBNAIL_TABLE:
            table_name = "thumbnail";
            break;
        case DatabaseConfig::VIDEO_TABLE:
            table_name = "video";
            break;
        default:
            table_name = "unknown";
            break;
        }

        std::string access_name;
        switch (access_type)
        {
        case Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE:
            access_name = "rw";
            break;
        case Database::SQLITE_ACCESS_OPEN_READ_ONLY:
            access_name = "ro";
            break;
        default:
            access_name = "unknown";
            break;
        }

        return table_name + "_" + access_name;
    }

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
    static DatabaseManager::ManagerResult<bool> initialize(const DatabaseManagerConfig &config)
    {
        if (DatabaseManager::get_instance().is_initialized())
        {
            return true; // Already initialized
        }
        return DatabaseManager::get_instance().initialize(config);
    }

    // Get factory name IDs for SQL databases (creates database if needed)
    static DatabaseManager::ManagerResult<std::string> get_faiss_table_factory_name(
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
    {
        auto &manager = DatabaseManager::get_instance();

        // First ensure the database is created
        auto db_result = manager.get_or_create_sql_database(DatabaseConfig::FAISS_TABLE, access_type);
        if (!db_result)
        {
            return tl::unexpected(db_result.error());
        }

        // Return the factory name
        return manager.get_sql_factory_name(DatabaseConfig::FAISS_TABLE, access_type);
    }

    static DatabaseManager::ManagerResult<std::string> get_thumbnail_table_factory_name(
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
    {
        auto &manager = DatabaseManager::get_instance();

        // First ensure the database is created
        auto db_result = manager.get_or_create_sql_database(DatabaseConfig::THUMBNAIL_TABLE, access_type);
        if (!db_result)
        {
            return tl::unexpected(db_result.error());
        }

        // Return the factory name
        return manager.get_sql_factory_name(DatabaseConfig::THUMBNAIL_TABLE, access_type);
    }

    static DatabaseManager::ManagerResult<std::string> get_video_table_factory_name(
        Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
    {
        auto &manager = DatabaseManager::get_instance();

        // First ensure the database is created
        auto db_result = manager.get_or_create_sql_database(DatabaseConfig::VIDEO_TABLE, access_type);
        if (!db_result)
        {
            return tl::unexpected(db_result.error());
        }

        // Return the factory name
        return manager.get_sql_factory_name(DatabaseConfig::VIDEO_TABLE, access_type);
    }

    // Quick access to FAISS indices
    static DatabaseManager::ManagerResult<FaissDatabaseQuickAccess::DatabasePtr> get_faiss_index_by_name(
        const std::string &factory_name)
    {
        return DatabaseManager::get_instance().get_faiss_index_by_name(factory_name);
    }

    // Utility functions
    static std::vector<std::string> get_available_faiss_factories()
    {
        return DatabaseManager::get_instance().get_available_faiss_factories();
    }

    static bool is_initialized()
    {
        return DatabaseManager::get_instance().is_initialized();
    }
};
