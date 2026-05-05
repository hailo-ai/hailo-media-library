#include "faiss_factory.hpp"

FaissVectorDBFactory::FactoryErrorInfo::FactoryErrorInfo(FactoryError t, const std::string &msg) : type(t), message(msg)
{
}

FaissDatabaseConfig::FaissDatabaseConfig(int dim, const std::string &db_dir, const std::string &file_pref,
                                         const std::vector<std::string> &cold_files, bool auto_flush_enabled,
                                         size_t threshold)
    : dimension(dim), db_directory(db_dir), file_prefix(file_pref), initial_cold_files(cold_files),
      auto_flush(auto_flush_enabled), flush_threshold(threshold)
{
}

FaissVectorDBFactory::FactoryResult<FaissVectorDBFactory::DatabasePtr> FaissVectorDBFactory::get_or_create_database(
    const std::string &name, const FaissDatabaseConfig &config)
{
    std::unique_lock<std::shared_mutex> lock(m_instances_mutex);

    auto it = m_instances.find(name);
    if (it != m_instances.end())
    {
        // Instance already exists, verify configuration compatibility
        auto stored_config = m_configs.find(name);
        if (stored_config != m_configs.end())
        {
            if (!is_configuration_compatible(*stored_config->second, config))
            {
                return tl::unexpected(FactoryErrorInfo(
                    FactoryError::INVALID_CONFIG, "Existing instance '" + name + "' has incompatible configuration"));
            }
        }
        return it->second;
    }

    // Create new instance
    auto db_result = PartitionedFaissDB::create(config.dimension, config.db_directory, config.file_prefix,
                                                config.initial_cold_files, config.auto_flush, config.flush_threshold);

    if (!db_result)
    {
        return tl::unexpected(
            FactoryErrorInfo(FactoryError::CREATION_FAILED, "Failed to create database: " + db_result.error().message));
    }

    // Store the instance and configuration
    auto shared_db = std::shared_ptr<PartitionedFaissDB>(std::move(db_result.value()));
    m_instances[name] = shared_db;
    m_configs[name] = std::make_shared<FaissDatabaseConfig>(config);

    HAILO_ANALYTICS_LOG_INFO("Created PartitionedFaissDB instance: {}", name);

    return shared_db;
}

FaissVectorDBFactory::FactoryResult<FaissVectorDBFactory::DatabasePtr> FaissVectorDBFactory::get_database(
    const std::string &name)
{
    std::shared_lock<std::shared_mutex> lock(m_instances_mutex);

    auto it = m_instances.find(name);
    if (it == m_instances.end())
    {
        return tl::unexpected(
            FactoryErrorInfo(FactoryError::INSTANCE_NOT_FOUND, "Database instance '" + name + "' not found"));
    }

    return it->second;
}

FaissVectorDBFactory::FactoryResult<FaissVectorDBFactory::DatabasePtr> FaissVectorDBFactory::create_database(
    const std::string &name, const FaissDatabaseConfig &config)
{
    std::unique_lock<std::shared_mutex> lock(m_instances_mutex);

    if (m_instances.find(name) != m_instances.end())
    {
        return tl::unexpected(
            FactoryErrorInfo(FactoryError::INSTANCE_ALREADY_EXISTS, "Database instance '" + name + "' already exists"));
    }

    auto db_result = PartitionedFaissDB::create(config.dimension, config.db_directory, config.file_prefix,
                                                config.initial_cold_files, config.auto_flush, config.flush_threshold);

    if (!db_result)
    {
        return tl::unexpected(
            FactoryErrorInfo(FactoryError::CREATION_FAILED, "Failed to create database: " + db_result.error().message));
    }

    auto shared_db = std::shared_ptr<PartitionedFaissDB>(db_result.value().release());
    m_instances[name] = shared_db;
    m_configs[name] = std::make_shared<FaissDatabaseConfig>(config);

    return shared_db;
}

bool FaissVectorDBFactory::remove_database(const std::string &name)
{
    std::unique_lock<std::shared_mutex> lock(m_instances_mutex);

    auto it = m_instances.find(name);
    if (it == m_instances.end())
    {
        return false;
    }

    m_instances.erase(it);
    m_configs.erase(name);
    return true;
}

bool FaissVectorDBFactory::database_exists(const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(m_instances_mutex);
    return m_instances.find(name) != m_instances.end();
}

std::vector<std::string> FaissVectorDBFactory::get_all_database_names() const
{
    std::shared_lock<std::shared_mutex> lock(m_instances_mutex);

    std::vector<std::string> names;
    names.reserve(m_instances.size());

    for (const auto &pair : m_instances)
    {
        names.push_back(pair.first);
    }

    return names;
}

FaissVectorDBFactory::FactoryResult<FaissVectorDBFactory::ConfigPtr> FaissVectorDBFactory::get_database_config(
    const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(m_instances_mutex);

    auto it = m_configs.find(name);
    if (it == m_configs.end())
    {
        return tl::unexpected(
            FactoryErrorInfo(FactoryError::INSTANCE_NOT_FOUND, "Database instance '" + name + "' not found"));
    }

    return it->second;
}

void FaissVectorDBFactory::clear_all()
{
    std::unique_lock<std::shared_mutex> lock(m_instances_mutex);
    m_instances.clear();
    m_configs.clear();
}

size_t FaissVectorDBFactory::get_database_count() const
{
    std::shared_lock<std::shared_mutex> lock(m_instances_mutex);
    return m_instances.size();
}

bool FaissVectorDBFactory::is_configuration_compatible(const FaissDatabaseConfig &existing,
                                                       const FaissDatabaseConfig &new_config) const
{
    return existing.dimension == new_config.dimension && existing.file_prefix == new_config.file_prefix;
}

// FaissDatabaseQuickAccess methods
FaissVectorDBFactory::FactoryResult<FaissDatabaseQuickAccess::DatabasePtr> FaissDatabaseQuickAccess::get_database(
    const std::string &name)
{
    return FaissVectorDBFactory::get_instance().get_database(name);
}

FaissVectorDBFactory::FactoryResult<FaissDatabaseQuickAccess::DatabasePtr> FaissDatabaseQuickAccess::create_database(
    const std::string &name, const FaissDatabaseConfig &config)
{
    return FaissVectorDBFactory::get_instance().create_database(name, config);
}

FaissVectorDBFactory::FactoryResult<FaissDatabaseQuickAccess::DatabasePtr> FaissDatabaseQuickAccess::
    get_or_create_database(const std::string &name, const FaissDatabaseConfig &config)
{
    return FaissVectorDBFactory::get_instance().get_or_create_database(name, config);
}

FaissVectorDBFactory::FactoryResult<FaissVectorDBFactory::ConfigPtr> FaissDatabaseQuickAccess::get_database_config(
    const std::string &name)
{
    return FaissVectorDBFactory::get_instance().get_database_config(name);
}

bool FaissDatabaseQuickAccess::remove_database(const std::string &name)
{
    return FaissVectorDBFactory::get_instance().remove_database(name);
}

bool FaissDatabaseQuickAccess::database_exists(const std::string &name)
{
    return FaissVectorDBFactory::get_instance().database_exists(name);
}

std::vector<std::string> FaissDatabaseQuickAccess::get_all_database_names()
{
    return FaissVectorDBFactory::get_instance().get_all_database_names();
}

void FaissDatabaseQuickAccess::clear_all()
{
    FaissVectorDBFactory::get_instance().clear_all();
}

size_t FaissDatabaseQuickAccess::get_database_count()
{
    return FaissVectorDBFactory::get_instance().get_database_count();
}
