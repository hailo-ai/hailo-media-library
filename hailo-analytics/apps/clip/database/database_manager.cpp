#include "database_manager.hpp"

#include "common_utils.hpp"

DatabaseManagerConfig::DatabaseManagerConfig(const std::string &sql_file_path, const std::string &faiss_path)
    : sql_db_file_path(sql_file_path), faiss_db_path(faiss_path)
{
}

void DatabaseManagerConfig::add_sql_factory(DatabaseConfig::DatabaseTable table_type,
                                            Database::SqliteAccessType access_type)
{
    sql_factories.push_back({table_type, access_type});
}

void DatabaseManagerConfig::add_faiss_factory(const std::string &name, int dimension)
{
    faiss_factories.push_back({name, dimension});
}

void DatabaseManagerConfig::add_all_common_sql_factories()
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

DatabaseManager::ManagerErrorInfo::ManagerErrorInfo(ManagerError t, const std::string &msg) : type(t), message(msg)
{
}

DatabaseManager::DatabaseManager() : m_is_initialized(false)
{
}

std::string DatabaseManager::get_sql_factory_name(DatabaseConfig::DatabaseTable table_type,
                                                  Database::SqliteAccessType access_type) const
{
    return generate_sql_factory_name(table_type, access_type);
}

std::vector<std::string> DatabaseManager::get_available_faiss_factories() const
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

std::vector<std::string> DatabaseManager::get_sql_factory_names() const
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

bool DatabaseManager::is_initialized() const
{
    std::shared_lock<std::shared_mutex> lock(m_manager_mutex);
    return m_is_initialized;
}

void DatabaseManager::reset()
{
    std::unique_lock<std::shared_mutex> lock(m_manager_mutex);

    m_sql_factory_names.clear();
    m_faiss_factory_names.clear();
    m_sql_db_file_path.clear();
    m_faiss_db_path.clear();
    m_is_initialized = false;
}

DatabaseManager::ManagerResult<bool> DatabaseManager::initialize(const DatabaseManagerConfig &config)
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
            return tl::unexpected(
                ManagerErrorInfo(ManagerError::SQL_DATABASE_ERROR,
                                 "Failed to create SQL database '" + factory_name + "': " + result.error().message));
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

DatabaseManager::ManagerResult<std::shared_ptr<Database>> DatabaseManager::get_or_create_sql_database(
    DatabaseConfig::DatabaseTable table_type, Database::SqliteAccessType access_type)
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

DatabaseManager::ManagerResult<std::shared_ptr<Database>> DatabaseManager::get_sql_database_by_name(
    const std::string &factory_name)
{
    std::shared_lock<std::shared_mutex> lock(m_manager_mutex);

    if (!m_is_initialized)
    {
        return tl::unexpected(ManagerErrorInfo(ManagerError::NOT_INITIALIZED, "DatabaseManager not initialized"));
    }

    auto it = m_sql_factory_names.find(factory_name);
    if (it == m_sql_factory_names.end())
    {
        return tl::unexpected(
            ManagerErrorInfo(ManagerError::FACTORY_NOT_FOUND, "SQL database factory '" + factory_name + "' not found"));
    }

    return SqlDatabaseQuickAccess::get_database(it->first).value();
}

DatabaseManager::ManagerResult<FaissDatabaseQuickAccess::DatabasePtr> DatabaseManager::get_faiss_index_by_name(
    const std::string &factory_name)
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

std::string DatabaseManager::generate_sql_factory_name(DatabaseConfig::DatabaseTable table_type,
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

// DatabaseManagerHelper implementation
DatabaseManager::ManagerResult<bool> DatabaseManagerHelper::initialize(const DatabaseManagerConfig &config)
{
    if (DatabaseManager::get_instance().is_initialized())
    {
        return true; // Already initialized
    }
    return DatabaseManager::get_instance().initialize(config);
}

DatabaseManager::ManagerResult<std::string> DatabaseManagerHelper::get_faiss_table_factory_name(
    Database::SqliteAccessType access_type)
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

DatabaseManager::ManagerResult<std::string> DatabaseManagerHelper::get_thumbnail_table_factory_name(
    Database::SqliteAccessType access_type)
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

DatabaseManager::ManagerResult<std::string> DatabaseManagerHelper::get_video_table_factory_name(
    Database::SqliteAccessType access_type)
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

DatabaseManager::ManagerResult<FaissDatabaseQuickAccess::DatabasePtr> DatabaseManagerHelper::get_faiss_index_by_name(
    const std::string &factory_name)
{
    return DatabaseManager::get_instance().get_faiss_index_by_name(factory_name);
}

std::vector<std::string> DatabaseManagerHelper::get_available_faiss_factories()
{
    return DatabaseManager::get_instance().get_available_faiss_factories();
}

bool DatabaseManagerHelper::is_initialized()
{
    return DatabaseManager::get_instance().is_initialized();
}
