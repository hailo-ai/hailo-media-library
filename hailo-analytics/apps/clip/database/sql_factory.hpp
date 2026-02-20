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
                   Database::SqliteAccessType access_type = Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
        : table(table_type), db_file_path(file_path), db_access_type(access_type)
    {
    }
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

        FactoryErrorInfo(FactoryError t, const std::string &msg) : type(t), message(msg)
        {
        }
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
    FactoryResult<DatabasePtr> get_or_create_database(const std::string &name, const DatabaseConfig &config)
    {
        std::unique_lock<std::shared_mutex> lock(instances_mutex_);

        auto it = instances_.find(name);
        if (it != instances_.end())
        {
            // Instance already exists, verify configuration compatibility
            auto stored_config = configs_.find(name);
            if (stored_config != configs_.end())
            {
                if (!is_configuration_compatible(*stored_config->second, config))
                {
                    return tl::unexpected(
                        FactoryErrorInfo(FactoryError::INVALID_CONFIG,
                                         "Existing instance '" + name + "' has incompatible configuration"));
                }
            }
            return it->second;
        }

        std::shared_ptr<Database> db_table;
        switch (config.table)
        {
        case DatabaseConfig::FAISS_TABLE:
            db_table = std::make_shared<FaissTable>(config.db_file_path, config.db_access_type);
            break;
        case DatabaseConfig::THUMBNAIL_TABLE:
            db_table = std::make_shared<ThumbnailTable>(config.db_file_path, config.db_access_type);
            break;
        case DatabaseConfig::VIDEO_TABLE:
            db_table = std::make_shared<VideoTable>(config.db_file_path, config.db_access_type);
            break;
        }

        if (!db_table->open())
            return tl::unexpected(FactoryErrorInfo(FactoryError::CREATION_FAILED, "Failed to create database"));

        if (config.db_access_type == Database::SQLITE_ACCESS_OPEN_CREATE_READ_WRITE)
        {
            if (!db_table->create_tables())
                return tl::unexpected(
                    FactoryErrorInfo(FactoryError::CREATION_FAILED, "Failed to create database table"));
        }
        else
        {
            if (!db_table->table_exists())
                return tl::unexpected(
                    FactoryErrorInfo(FactoryError::INTEGRITY_CHECK_FAILED, "Database table does not exist"));
        }

        // Store the instance and configuration
        instances_[name] = db_table;
        configs_[name] = std::make_shared<DatabaseConfig>(config);

        HAILO_ANALYTICS_LOG_INFO("Created Database instance: {}", name);

        return db_table;
    }

    // Get existing database instance
    FactoryResult<DatabasePtr> get_database(const std::string &name)
    {
        std::shared_lock<std::shared_mutex> lock(instances_mutex_);

        auto it = instances_.find(name);
        if (it == instances_.end())
        {
            return tl::unexpected(
                FactoryErrorInfo(FactoryError::INSTANCE_NOT_FOUND, "Database instance '" + name + "' not found"));
        }

        return it->second;
    }

    // Remove database instance
    bool remove_database(const std::string &name)
    {
        std::unique_lock<std::shared_mutex> lock(instances_mutex_);

        auto it = instances_.find(name);
        if (it == instances_.end())
        {
            return false;
        }

        instances_.erase(it);
        configs_.erase(name);
        return true;
    }

    // Check if database exists
    bool database_exists(const std::string &name) const
    {
        std::shared_lock<std::shared_mutex> lock(instances_mutex_);
        return instances_.find(name) != instances_.end();
    }

    // Get all database names
    std::vector<std::string> get_all_database_names() const
    {
        std::shared_lock<std::shared_mutex> lock(instances_mutex_);

        std::vector<std::string> names;
        names.reserve(instances_.size());

        for (const auto &pair : instances_)
        {
            names.push_back(pair.first);
        }

        return names;
    }

    // Get configuration for a database
    FactoryResult<ConfigPtr> get_database_config(const std::string &name) const
    {
        std::shared_lock<std::shared_mutex> lock(instances_mutex_);

        auto it = configs_.find(name);
        if (it == configs_.end())
        {
            return tl::unexpected(
                FactoryErrorInfo(FactoryError::INSTANCE_NOT_FOUND, "Database instance '" + name + "' not found"));
        }

        return it->second;
    }

    // Clear all databases (removes all instances)
    void clear_all()
    {
        std::unique_lock<std::shared_mutex> lock(instances_mutex_);
        instances_.clear();
        configs_.clear();
    }

    // Get number of managed databases
    size_t get_database_count() const
    {
        std::shared_lock<std::shared_mutex> lock(instances_mutex_);
        return instances_.size();
    }

  private:
    // Private constructor for singleton
    DBFactory() = default;

    // Non-copyable, non-movable
    DBFactory(const DBFactory &) = delete;
    DBFactory &operator=(DBFactory &&) = delete;

    // Check if configurations are compatible
    bool is_configuration_compatible(const DatabaseConfig &existing, const DatabaseConfig &new_config) const
    {
        return existing.table == new_config.table && existing.db_file_path == new_config.db_file_path &&
               existing.db_access_type == new_config.db_access_type;
    }

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
    static DBFactory::FactoryResult<DBFactory::DatabasePtr> get_database(const std::string &name)
    {
        return DBFactory::get_instance().get_database(name);
    }

    static DBFactory::FactoryResult<DBFactory::DatabasePtr> get_or_create_database(const std::string &name,
                                                                                   const DatabaseConfig &config)
    {
        return DBFactory::get_instance().get_or_create_database(name, config);
    }

    static bool remove_database(const std::string &name)
    {
        return DBFactory::get_instance().remove_database(name);
    }

    static bool database_exists(const std::string &name)
    {
        return DBFactory::get_instance().database_exists(name);
    }

    static std::vector<std::string> get_all_database_names()
    {
        return DBFactory::get_instance().get_all_database_names();
    }

    static void clear_all()
    {
        DBFactory::get_instance().clear_all();
    }

    static size_t get_database_count()
    {
        return DBFactory::get_instance().get_database_count();
    }
};
