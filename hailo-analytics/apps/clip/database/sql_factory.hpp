#pragma once

#include "faiss_table.hpp"
#include "thumbnail_table.hpp"
#include "video_table.hpp"
#include "faiss_factory.hpp"

#include <thread>
#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <tl/expected.hpp>

// Configuration struct for database creation
struct DatabaseConfig
{
    enum DatabaseTable
    {
        FAISS_TABLE,
        THUMBNAIL_TABLE,
        VIDEO_TABLE,
    };

    DatabaseTable table;
    std::string db_file_path;
    Database::SqliteAccessType db_access_type;

    DatabaseConfig(DatabaseTable table_type, const std::string &file_path = "",
                   Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE);
};

// Factory class for managing table instances
class DBFactory
{
  public:
    using DatabasePtr = std::shared_ptr<Database>;
    using ConfigPtr = std::shared_ptr<DatabaseConfig>;

    // Error types for factory operations
    enum class FactoryError
    {
        INSTANCE_NOT_FOUND,
        INSTANCE_ALREADY_EXISTS,
        CREATION_FAILED,
        INTEGRITY_CHECK_FAILED,
        INVALID_CONFIG
    };

    struct FactoryErrorInfo
    {
        FactoryError type;
        std::string message;

        FactoryErrorInfo(FactoryError t, const std::string &msg);
    };

    template <typename T> using FactoryResult = tl::expected<T, FactoryErrorInfo>;

    // Singleton access to factory
    static DBFactory &get_instance()
    {
        static DBFactory instance;
        return instance;
    }

    // Create or get existing database instance as base
    // Make sure to dynamic cast it (eg auto faiss_db_Ptr = std::dynamic_pointer_cast<FaissTable>(db_table))
    FactoryResult<DatabasePtr> get_or_create_database(const std::string &name, const DatabaseConfig &config);

    // Get existing database instance
    FactoryResult<DatabasePtr> get_database(const std::string &name);

    // Remove database instance
    bool remove_database(const std::string &name);

    // Check if database exists
    bool database_exists(const std::string &name) const;

    // Get all database names
    std::vector<std::string> get_all_database_names() const;

    // Get configuration for a database
    FactoryResult<ConfigPtr> get_database_config(const std::string &name) const;

    // Clear all databases (removes all instances)
    void clear_all();

    // Get number of managed databases
    size_t get_database_count() const;

  private:
    // Private constructor for singleton
    DBFactory() = default;

    // Non-copyable, non-movable
    DBFactory(const DBFactory &) = delete;
    DBFactory &operator=(DBFactory &&) = delete;

    // Check if configurations are compatible
    bool is_configuration_compatible(const DatabaseConfig &existing, const DatabaseConfig &new_config) const;

    // Storage for instances and configurations
    std::unordered_map<std::string, DatabasePtr> instances_;
    std::unordered_map<std::string, ConfigPtr> configs_;

    // Thread synchronization
    mutable std::shared_mutex instances_mutex_;
};

// Convenience wrapper class for easier usage
class SqlDatabaseQuickAccess
{
  public:
    // Static methods for common operations
    static DBFactory::FactoryResult<DBFactory::DatabasePtr> get_database(const std::string &name);

    static DBFactory::FactoryResult<DBFactory::DatabasePtr> get_or_create_database(const std::string &name,
                                                                                   const DatabaseConfig &config);

    static bool remove_database(const std::string &name);

    static bool database_exists(const std::string &name);

    static std::vector<std::string> get_all_database_names();

    static void clear_all();

    static size_t get_database_count();
};
