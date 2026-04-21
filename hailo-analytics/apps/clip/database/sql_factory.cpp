#include "sql_factory.hpp"

DatabaseConfig::DatabaseConfig(DatabaseTable table_type, const std::string &file_path,
                               Database::SqliteAccessType access_type)
    : table(table_type), db_file_path(file_path), db_access_type(access_type)
{
}

DBFactory::FactoryErrorInfo::FactoryErrorInfo(FactoryError t, const std::string &msg) : type(t), message(msg)
{
}

DBFactory::FactoryResult<DBFactory::DatabasePtr> DBFactory::get_or_create_database(const std::string &name,
                                                                                   const DatabaseConfig &config)
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
                return tl::unexpected(FactoryErrorInfo(
                    FactoryError::INVALID_CONFIG, "Existing instance '" + name + "' has incompatible configuration"));
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
            return tl::unexpected(FactoryErrorInfo(FactoryError::CREATION_FAILED, "Failed to create database table"));
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

DBFactory::FactoryResult<DBFactory::DatabasePtr> DBFactory::get_database(const std::string &name)
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

bool DBFactory::remove_database(const std::string &name)
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

bool DBFactory::database_exists(const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(instances_mutex_);
    return instances_.find(name) != instances_.end();
}

std::vector<std::string> DBFactory::get_all_database_names() const
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

DBFactory::FactoryResult<DBFactory::ConfigPtr> DBFactory::get_database_config(const std::string &name) const
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

void DBFactory::clear_all()
{
    std::unique_lock<std::shared_mutex> lock(instances_mutex_);
    instances_.clear();
    configs_.clear();
}

size_t DBFactory::get_database_count() const
{
    std::shared_lock<std::shared_mutex> lock(instances_mutex_);
    return instances_.size();
}

bool DBFactory::is_configuration_compatible(const DatabaseConfig &existing, const DatabaseConfig &new_config) const
{
    return existing.table == new_config.table && existing.db_file_path == new_config.db_file_path &&
           existing.db_access_type == new_config.db_access_type;
}

// SqlDatabaseQuickAccess wrapper methods implementation
DBFactory::FactoryResult<DBFactory::DatabasePtr> SqlDatabaseQuickAccess::get_database(const std::string &name)
{
    return DBFactory::get_instance().get_database(name);
}

DBFactory::FactoryResult<DBFactory::DatabasePtr> SqlDatabaseQuickAccess::get_or_create_database(
    const std::string &name, const DatabaseConfig &config)
{
    return DBFactory::get_instance().get_or_create_database(name, config);
}

bool SqlDatabaseQuickAccess::remove_database(const std::string &name)
{
    return DBFactory::get_instance().remove_database(name);
}

bool SqlDatabaseQuickAccess::database_exists(const std::string &name)
{
    return DBFactory::get_instance().database_exists(name);
}

std::vector<std::string> SqlDatabaseQuickAccess::get_all_database_names()
{
    return DBFactory::get_instance().get_all_database_names();
}

void SqlDatabaseQuickAccess::clear_all()
{
    DBFactory::get_instance().clear_all();
}

size_t SqlDatabaseQuickAccess::get_database_count()
{
    return DBFactory::get_instance().get_database_count();
}
