#pragma once

#include "faiss_partitioned.hpp"
#include <iostream>
#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <functional>

// Configuration struct for database creation
struct FaissDatabaseConfig
{
    int dimension;
    const std::string db_directory;
    const std::string file_prefix;
    const std::vector<std::string> initial_cold_files;
    bool auto_flush;
    size_t flush_threshold;

    FaissDatabaseConfig(int dim, const std::string &db_dir = "", const std::string &file_pref = "",
                        const std::vector<std::string> &cold_files = {}, bool auto_flush_enabled = true,
                        size_t threshold = 1000);
};

// Factory class for managing PartitionedFaissDB instances
class FaissVectorDBFactory
{
  public:
    using DatabasePtr = std::shared_ptr<PartitionedFaissDB>;
    using ConfigPtr = std::shared_ptr<FaissDatabaseConfig>;

    // Error types for factory operations
    enum class FactoryError
    {
        INSTANCE_NOT_FOUND,
        INSTANCE_ALREADY_EXISTS,
        CREATION_FAILED,
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
    static FaissVectorDBFactory &get_instance()
    {
        static FaissVectorDBFactory instance;
        return instance;
    }

    FactoryResult<DatabasePtr> get_or_create_database(const std::string &name, const FaissDatabaseConfig &config);
    FactoryResult<DatabasePtr> get_database(const std::string &name);
    FactoryResult<DatabasePtr> create_database(const std::string &name, const FaissDatabaseConfig &config);
    bool remove_database(const std::string &name);
    bool database_exists(const std::string &name) const;
    std::vector<std::string> get_all_database_names() const;
    FactoryResult<ConfigPtr> get_database_config(const std::string &name) const;
    void clear_all();
    size_t get_database_count() const;

  private:
    // Private constructor for singleton
    FaissVectorDBFactory() = default;

    // Non-copyable, non-movable
    FaissVectorDBFactory(const FaissVectorDBFactory &) = delete;
    FaissVectorDBFactory &operator=(const FaissVectorDBFactory &) = delete;
    FaissVectorDBFactory(FaissVectorDBFactory &&) = delete;
    FaissVectorDBFactory &operator=(FaissVectorDBFactory &&) = delete;

    bool is_configuration_compatible(const FaissDatabaseConfig &existing, const FaissDatabaseConfig &new_config) const;

    // Storage for instances and configurations
    std::unordered_map<std::string, DatabasePtr> m_instances;
    std::unordered_map<std::string, ConfigPtr> m_configs;

    // Thread synchronization
    mutable std::shared_mutex m_instances_mutex;
};

// Convenience wrapper class for easier usage
class FaissDatabaseQuickAccess
{
  public:
    using DatabasePtr = FaissVectorDBFactory::DatabasePtr;

    static FaissVectorDBFactory::FactoryResult<DatabasePtr> get_database(const std::string &name);
    static FaissVectorDBFactory::FactoryResult<DatabasePtr> create_database(const std::string &name,
                                                                            const FaissDatabaseConfig &config);
    static FaissVectorDBFactory::FactoryResult<DatabasePtr> get_or_create_database(const std::string &name,
                                                                                   const FaissDatabaseConfig &config);
    static FaissVectorDBFactory::FactoryResult<FaissVectorDBFactory::ConfigPtr> get_database_config(
        const std::string &name);
    static bool remove_database(const std::string &name);
    static bool database_exists(const std::string &name);
    static std::vector<std::string> get_all_database_names();
    static void clear_all();
    static size_t get_database_count();

    // Helper function to handle errors
    template <typename T>
    static bool handle_faiss_result(const PartitionedFaissDB::Result<T> &result, const std::string &operation)
    {
        if (!result)
        {
            HAILO_ANALYTICS_LOG_ERROR("X {} failed: {}", operation, result.error().message);
            return false;
        }
        return true;
    }
};

// Example usage and test functions
#if 0
namespace Examples {

    void basicFactoryUsage() {
        std::cout << "=== Basic Factory Usage ===" << std::endl;

        // Create databases with different configurations
        FaissDatabaseConfig config1(128, "embeddings.index", true, 1000);
        FaissDatabaseConfig config2(256, "features.index", true, 500);

        auto db1_result = FaissDatabaseQuickAccess::create_database("embeddings", config1);
        auto db2_result = FaissDatabaseQuickAccess::create_database("features", config2);

        if (db1_result && db2_result) {
            auto db1 = db1_result.value();
            auto db2 = db2_result.value();

            // Insert some test data
            std::vector<float> vec1(128, 1.0f);
            std::vector<float> vec2(256, 2.0f);

            db1->insert(vec1);
            db2->insert(vec2);

            std::cout << "Created and populated databases" << std::endl;
            std::cout << "Database count: " << FaissDatabaseQuickAccess::get_database_count() << std::endl;

            // List all databases
            auto names = FaissDatabaseQuickAccess::get_all_database_names();
            std::cout << "Database names: ";
            for (const auto& name : names) {
                std::cout << name << " ";
            }
            std::cout << std::endl;
        }
    }

    void multiThreadedFactoryUsage() {
        std::cout << "\n=== Multi-threaded Factory Usage ===" << std::endl;

        FaissDatabaseConfig config(128, "shared.index", true, 100);

        // Multiple threads trying to get/create the same database
        std::vector<std::thread> threads;

        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&config, i]() {
                auto db_result = FaissDatabaseQuickAccess::get_or_create_database("shared_db", config);
                if (db_result) {
                    auto db = db_result.value();

                    // Insert some data
                    std::vector<float> vec(128, static_cast<float>(i));
                    auto insert_result = db->insert(vec);

                    if (insert_result) {
                        std::cout << "Thread " << i << " inserted vector with ID: "
                                 << insert_result.value() << std::endl;
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        // Check final state
        auto db_result = FaissDatabaseQuickAccess::get_database("shared_db");
        if (db_result) {
            auto db = db_result.value();
            std::cout << "Final database size: " << db->size() << std::endl;
        }
    }

    void factoryErrorHandling() {
        std::cout << "\n=== Factory Error Handling ===" << std::endl;

        // Try to get non-existent database
        auto db_result = FaissDatabaseQuickAccess::get_database("non_existent");
        if (!db_result) {
            std::cout << "Expected error: " << db_result.error().message << std::endl;
        }

        // Try to create database with same name twice
        FaissDatabaseConfig config(64, "test.index");
        auto create1 = FaissDatabaseQuickAccess::create_database("test_db", config);
        auto create2 = FaissDatabaseQuickAccess::create_database("test_db", config);

        if (create1) {
            std::cout << "First creation succeeded" << std::endl;
        }

        if (!create2) {
            std::cout << "Expected error on second creation: " << create2.error().message << std::endl;
        }
    }

    void factoryStatistics() {
        std::cout << "\n=== Factory Statistics ===" << std::endl;

        // Create multiple databases
        FaissDatabaseConfig config1(100, "stats1.index");
        FaissDatabaseConfig config2(200, "stats2.index");

        auto db1 = FaissDatabaseQuickAccess::create_database("stats_db1", config1);
        auto db2 = FaissDatabaseQuickAccess::create_database("stats_db2", config2);

        if (db1 && db2) {
            // Add some data
            std::vector<float> vec1(100, 1.0f);
            std::vector<float> vec2(200, 2.0f);

            db1.value()->insert(vec1);
            db2.value()->insert(vec2);
        }
    }

    void runAllExamples() {
        // Clean up before running examples
        FaissDatabaseQuickAccess::clear_all();

        basicFactoryUsage();
        multiThreadedFactoryUsage();
        factoryErrorHandling();
        factoryStatistics();

        // Clean up after examples
        std::cout << "\n=== Cleanup ===" << std::endl;
        std::cout << "Databases before cleanup: " << FaissDatabaseQuickAccess::get_database_count() << std::endl;
        FaissDatabaseQuickAccess::clear_all();
        std::cout << "Databases after cleanup: " << FaissDatabaseQuickAccess::get_database_count() << std::endl;
    }
}
#endif
