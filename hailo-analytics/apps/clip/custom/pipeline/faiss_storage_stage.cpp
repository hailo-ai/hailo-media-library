#include "faiss_storage_stage.hpp"

#include "hailo_analytics/pipeline/core/error_utils.hpp"

#include <chrono>
#include <sstream>

FaissStorageStage::FaissStorageStage(std::string name, std::string db_path, std::string faiss_dir,
                                     std::string faiss_filename_prefix, size_t queue_size, bool leaky,
                                     bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_db_path(db_path), m_faiss_dir(faiss_dir), m_faiss_filename_prefix(faiss_filename_prefix),
      m_should_terminate(false)
{
    m_faiss_index_source = IDX_SOURCE_FROM_FILE;
    m_db_source = DB_SOURCE_FROM_FILE;
}

FaissStorageStage::FaissStorageStage(std::string name, std::string db_factory_name, size_t queue_size, bool leaky,
                                     bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_db_factory_name(db_factory_name), m_should_terminate(false)
{
    m_faiss_index_source = IDX_SOURCE_FROM_USER_META;
    m_db_source = DB_SOURCE_FROM_FACTORY;
}

hailo_analytics::pipeline::AppStatus FaissStorageStage::init()
{
    switch (m_db_source)
    {
    case DB_SOURCE_FROM_FACTORY: {
        auto faiss_table_result = SqlDatabaseQuickAccess::get_database(m_db_factory_name);
        if (!faiss_table_result)
        {
            std::cerr << "Faiss Storage " << m_stage_name << " database not found in factory" << std::endl;
            HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} database not found in factory", m_stage_name);
            return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
        }

        m_faiss_table = std::dynamic_pointer_cast<FaissTable>(faiss_table_result.value());
        break;
    }
    default:
        std::cerr << "Faiss Storage " << m_stage_name << " unsupported faiss database source" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} unsupported faiss database source", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR;
    }

    // Start the database access thread
    m_database_thread = std::thread(&FaissStorageStage::database_access, this);

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

hailo_analytics::pipeline::AppStatus FaissStorageStage::deinit()
{
    // Signal the thread to terminate
    m_should_terminate = true;
    m_data_cv.notify_all();

    // Wait for the thread to finish
    if (m_database_thread.joinable())
    {
        m_database_thread.join();
    }

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

void FaissStorageStage::loop()
{
    while (!m_end_of_stream)
    {
        bool all_queue_empty = true;
        for (auto &queue : m_queues)
        {
            if (queue->size())
            {
                BufferPtr data = queue->pop();
                if (process(data) == AppStatus::SUCCESS)
                {
                    send_to_subscribers(data);
                }
                else
                {
                    std::cerr << "Faiss Storage " << m_stage_name << " failed to process data" << std::endl;
                    HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} failed to process data", m_stage_name);
                }

                all_queue_empty = false;
            }

            if (all_queue_empty)
            {
                // Sleep for a short duration to avoid busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
}

hailo_analytics::pipeline::AppStatus FaissStorageStage::process(BufferPtr data)
{
    if (m_faiss_table == nullptr)
    {
        std::cerr << "Faiss Storage " << m_stage_name << " database failed to initialized" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} database failed to initialized", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::UNINITIALIZED;
    }

    HailoROIPtr roi = data->get_roi();

    // Get classification label
    std::string classification_label;
    auto classifications = roi->get_objects_typed(HAILO_CLASSIFICATION);
    if (classifications.size() != 1)
    {
        HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} expected exactly 1 classification, got {}", m_stage_name,
                                  classifications.size());
        return hailo_analytics::pipeline::AppStatus::PIPELINE_ERROR;
    }
    auto classification = std::dynamic_pointer_cast<HailoClassification>(classifications[0]);
    classification_label = classification->get_label();

    int64_t epoch_millis = get_current_epochmilliseconds();
    FaissDatabaseQuickAccess::DatabasePtr faiss_index = nullptr;
    std::string network_embedding_name;
    int32_t track_id = -1;

    // Get tracking ID
    for (auto obj : roi->get_objects_typed(HAILO_UNIQUE_ID))
    {
        HailoUniqueIDPtr id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
        if (id->get_mode() == TRACKING_ID)
        {
            track_id = id->get_id();
            break; // We only expect 1 tracking id
        }
    }

    if (track_id == -1)
    {
        std::cerr << "Faiss Storage " << m_stage_name << " tracking ID not found, skipping data" << std::endl;
        HAILO_ANALYTICS_LOG_WARN("Faiss Storage {} tracking ID not found, skipping data", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::SUCCESS;
    }

    switch (m_faiss_index_source)
    {
    case IDX_SOURCE_FROM_USER_META: {
        auto result = get_faiss_index_from_user_meta(roi);
        network_embedding_name = result.first;
        faiss_index = result.second;
        break;
    }
    default:
        std::cerr << "Faiss Storage " << m_stage_name << " unsupported faiss index source" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} unsupported faiss index source", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::CONFIGURATION_ERROR;
    }

    if (faiss_index == nullptr)
    {
        std::cerr << "Faiss Storage " << m_stage_name << " faiss index object not found" << std::endl;
        HAILO_ANALYTICS_LOG_ERROR("Faiss Storage {} faiss index object not found", m_stage_name);
        return hailo_analytics::pipeline::AppStatus::PIPELINE_ERROR;
    }

    if (roi->get_objects_typed(HAILO_MATRIX).size() > 1)
    {
        std::cout << "WARNING: Faiss Storage " << m_stage_name << " multiple matrix found, should only be one"
                  << std::endl;
        HAILO_ANALYTICS_LOG_WARN("Faiss Storage {} multiple matrix found, should only be one", m_stage_name);
    }

    for (auto obj : roi->get_objects_typed(HAILO_MATRIX))
    {
        HailoMatrixPtr matrix = std::dynamic_pointer_cast<HailoMatrix>(obj);

        // Add to Faiss Index
        auto insert_result = faiss_index->insert(matrix->get_data());
        if (!FaissDatabaseQuickAccess::handle_faiss_result(insert_result, "Insert with auto ID"))
        {
            break;
        }

        // Add to Database Table - Push to pending for batch insert
        {
            std::lock_guard<std::mutex> lock(m_data_mutex);
            m_pending_operations.emplace_back(insert_result.value(), track_id, epoch_millis, network_embedding_name,
                                              classification_label);
        }

        // Aaron Test TEMP ONLY - Generate index file of embedding and save it to file for later use as testing
#if 0
        {
            std::string filename = generate_filename(std::to_string(insert_result.value()), "emb");
            std::ofstream file = create_file(m_faiss_dir, filename);
            for (float value : matrix->get_data())
            {
                file << value << "\n"; // Write each float on a new line
            }
            file.close();
        }
#endif
        // Expecting only one, we will ignore the rest (if any)
        break;
    }

    return hailo_analytics::pipeline::AppStatus::SUCCESS;
}

void FaissStorageStage::database_access()
{
    std::chrono::high_resolution_clock::time_point last_insert_time = std::chrono::high_resolution_clock::now();
    std::vector<std::tuple<int64_t, int32_t, int64_t, std::string, std::string>> operations_to_process;

    while (!m_should_terminate.load())
    {
        {
            std::unique_lock<std::mutex> lock(m_data_mutex);
            // Wait for notification or timeout (to check termination flag)
            m_data_cv.wait_for(lock, std::chrono::milliseconds(100), [this] { return m_should_terminate.load(); });

            // Check if m_pending_operations reaches a certain size or time in ms has past
            // we move it to operations_to_process and release the data mutex lock right away
            if (m_should_terminate.load() || m_pending_operations.size() >= FAISS_DB_FLASH_MIN_SIZE ||
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() -
                                                                      last_insert_time)
                        .count() >= FAISS_DB_FLASH_INTERVAL_MS)
            {
                operations_to_process = std::move(m_pending_operations);
                m_pending_operations.clear();
                last_insert_time = std::chrono::high_resolution_clock::now();
            }
        }

        if (m_faiss_table && !operations_to_process.empty())
        {
            // Start measuring time
            auto start = std::chrono::high_resolution_clock::now();

            m_faiss_table->insert_batch(operations_to_process);

            // DEBUG Measure
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            if (duration.count() > 30)
            {
                HAILO_ANALYTICS_LOG_INFO("Time taken FAISS table batch insert: {} ms, total insert items: {}",
                                         duration.count(), operations_to_process.size());
            }
        }

        operations_to_process.clear();

        if (m_should_terminate.load())
            break;
    }
}

std::ofstream FaissStorageStage::create_file(const std::string &dir_path, const std::string &filename)
{
    fs::path full_path = fs::path(dir_path) / filename;

    // Open the file with desired flags
    std::ofstream file(full_path, std::ios::out | std::ios::binary | std::ios::app);

    return file;
}

int64_t FaissStorageStage::get_current_epochmilliseconds()
{
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return millis;
}

std::string FaissStorageStage::generate_filename(const std::string &prefix, const std::string &extension)
{
    // Construct the filename
    std::ostringstream filename;
    filename << prefix << "." << extension;

    return filename.str();
}

std::pair<std::string, FaissDatabaseQuickAccess::DatabasePtr> FaissStorageStage::get_faiss_index_from_user_meta(
    HailoROIPtr roi)
{
    std::string network_embedding_name;
    int network_embedding_size = 0;

    if (roi->get_objects_typed(HAILO_USER_META).size() > 1)
    {
        std::cout << "WARNING: Faiss Storage " << m_stage_name << " multiple user meta found, should only be one"
                  << std::endl;
        HAILO_ANALYTICS_LOG_WARN("Faiss Storage {} multiple user meta found, , should only be one", m_stage_name);
    }

    for (auto obj : roi->get_objects_typed(HAILO_USER_META))
    {
        HailoUserMetaPtr user_meta = std::dynamic_pointer_cast<HailoUserMeta>(obj);
        network_embedding_name = user_meta->get_user_string();
        network_embedding_size = user_meta->get_user_int();
        // std::cout << "Faiss network embedding name: " << network_embedding_name << " Embedding size: " <<
        // network_embedding_size << std::endl;

        // Expecting only one, we will ignore the rest (if any)
        break;
    }

    if (network_embedding_name.empty() || network_embedding_size == 0)
    {
        std::cout << "Faiss Storage " << m_stage_name << " network embedding name or size not found, skipping data"
                  << std::endl;
        HAILO_ANALYTICS_LOG_WARN("Faiss Storage {} network embedding name or size not found, skipping data",
                                 m_stage_name);
        return std::make_pair(network_embedding_name, nullptr);
    }

    auto index_result = FaissDatabaseQuickAccess::get_database(network_embedding_name);
    return std::make_pair(network_embedding_name, (index_result) ? index_result.value() : nullptr);
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_database_file_path(std::string filepath)
{
    m_db_path = filepath;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_db_factory_name(std::string name)
{
    m_db_factory_name = name;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_faiss_path(std::string path)
{
    m_faiss_dir = path;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_faiss_file_prefix(std::string prefix)
{
    m_faiss_filename_prefix = prefix;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_faiss_index_source(FaissIndexSource source)
{
    m_faiss_index_source = source;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_db_source(DBSource source)
{
    m_db_source = source;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

FaissStorageStageBuild::Builder &FaissStorageStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<FaissStorageStage> FaissStorageStageBuild::Builder::buildptr() const
{
    if (m_db_source != DBSource::DB_SOURCE_FROM_FACTORY ||
        m_faiss_index_source != FaissIndexSource::IDX_SOURCE_FROM_USER_META)
        THROW_IF_MISSING(false, "combination of set_faiss_index_source and set_db_source");

    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    if (m_db_source == DBSource::DB_SOURCE_FROM_FACTORY)
    {
        THROW_IF_MISSING(m_db_factory_name.has_value(), "set_db_factory_name");
    }

    return std::make_shared<FaissStorageStage>(m_stage_name.value(), m_db_factory_name.value(), m_queue_size, m_leaky,
                                               m_trace);
}

FaissStorageStageBuild::Builder FaissStorageStageBuild::create()
{
    return Builder();
}
