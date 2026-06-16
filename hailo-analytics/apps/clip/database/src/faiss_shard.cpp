#include "faiss_shard.hpp"

#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <mutex>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

FaissShard::Error::Error(ErrorType t, const std::string &msg) : type(t), message(msg)
{
}

FaissShard::SearchResult::SearchResult(faiss::idx_t id, float distance) : id(id), distance(distance)
{
}

void FaissShard::set_file_path(const std::string &path)
{
    m_index_file_path = path;
}

const std::string &FaissShard::get_file_path() const
{
    return m_index_file_path;
}

faiss::Index *FaissShard::get_index()
{
    return m_index.get();
}

const faiss::Index *FaissShard::get_index() const
{
    return m_index.get();
}

FaissShard::FaissShard(int dimension, const std::string &index_file_path)
    : m_dimension(dimension), m_index_file_path(index_file_path)
{
}

FaissShard::Result<std::shared_ptr<FaissShard>> FaissShard::create(int dimension, const std::string &index_file_path)
{
    if (dimension <= 0)
    {
        return tl::unexpected(Error(ErrorType::INVALID_DIMENSION, "Dimension must be positive"));
    }

    auto shard = std::shared_ptr<FaissShard>(new FaissShard(dimension, index_file_path));

    if (!index_file_path.empty())
    {
        auto load_result = shard->load_from_file();
        if (load_result)
        {
            HAILO_ANALYTICS_LOG_INFO("Loaded existing shard from: {}", index_file_path);
            return shard;
        }
        else
        {
            return tl::unexpected(
                Error(ErrorType::FILE_LOAD_ERROR, index_file_path + ": " + load_result.error().message));
        }
    }
    else
    {
        shard->create_new_index();
    }

    return shard;
}

FaissShard::Result<void> FaissShard::insert(const std::vector<float> &vector, faiss::idx_t id)
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
    if (vector.size() != static_cast<size_t>(m_dimension))
    {
        return tl::unexpected(Error(ErrorType::DIMENSION_MISMATCH, "Vector dimension mismatch"));
    }
    if (m_used_ids.count(id))
    {
        return tl::unexpected(
            Error(ErrorType::ID_ALREADY_EXISTS, "ID already exists in this shard: " + std::to_string(id)));
    }
    try
    {
        m_index->add_with_ids(1, vector.data(), &id);
        m_used_ids.insert(id);
    }
    catch (const std::exception &e)
    {
        return tl::unexpected(Error(ErrorType::FAISS_ERROR, "Failed to add vector: " + std::string(e.what())));
    }
    return {};
}

FaissShard::Result<std::vector<FaissShard::SearchResult>> FaissShard::search(const std::vector<float> &query,
                                                                             int k) const
{
    if (query.size() != static_cast<size_t>(m_dimension))
    {
        return tl::unexpected(Error(ErrorType::DIMENSION_MISMATCH, "Query vector dimension mismatch"));
    }
    if (k <= 0)
    {
        return tl::unexpected(Error(ErrorType::INVALID_K, "k must be positive"));
    }

    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    if (m_index->ntotal == 0)
        return std::vector<SearchResult>{};

    try
    {
        int effective_k = std::min(k, static_cast<int>(m_index->ntotal));
        std::vector<float> distances(effective_k);
        std::vector<faiss::idx_t> labels(effective_k);
        m_index->search(1, query.data(), effective_k, distances.data(), labels.data());
        std::vector<SearchResult> results;
        results.reserve(effective_k);
        for (int i = 0; i < effective_k; ++i)
        {
            if (labels[i] != -1)
                results.emplace_back(labels[i], distances[i]);
        }
        return results;
    }
    catch (const std::exception &e)
    {
        return tl::unexpected(Error(ErrorType::FAISS_ERROR, "Search failed: " + std::string(e.what())));
    }
}

FaissShard::Result<void> FaissShard::flush()
{
    if (m_index_file_path.empty())
    {
        return tl::unexpected(Error(ErrorType::NO_FILE_PATH, "No file path specified for flushing"));
    }

    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);

    try
    {
        faiss::write_index(m_index.get(), m_index_file_path.c_str());
        return {};
    }
    catch (const std::exception &e)
    {
        return tl::unexpected(
            Error(ErrorType::FILE_SAVE_ERROR, "Failed to flush shard to file: " + std::string(e.what())));
    }
}

FaissShard::Result<std::vector<float>> FaissShard::get_vector_by_id(faiss::idx_t id) const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    if (m_used_ids.count(id) == 0)
    {
        return tl::unexpected(Error(ErrorType::ID_NOT_FOUND, "ID not found in this shard: " + std::to_string(id)));
    }
    std::vector<float> vector_data(m_dimension);
    try
    {
        m_index->reconstruct(id, vector_data.data());
        return vector_data;
    }
    catch (const std::exception &e)
    {
        return tl::unexpected(Error(ErrorType::FAISS_ERROR, "Failed to get vector for ID " + std::to_string(id) + ": " +
                                                                std::string(e.what())));
    }
}

bool FaissShard::exists(faiss::idx_t id) const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    return m_used_ids.count(id) > 0;
}

size_t FaissShard::size() const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    return m_index->ntotal;
}

std::vector<faiss::idx_t> FaissShard::get_all_ids() const
{
    std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
    std::vector<faiss::idx_t> ids(m_used_ids.begin(), m_used_ids.end());
    std::sort(ids.begin(), ids.end());
    return ids;
}

void FaissShard::create_new_index()
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
    m_index = std::make_unique<faiss::IndexIDMap2>(new faiss::IndexFlatIP(m_dimension));
    m_index->own_fields = true;
    m_used_ids.clear();
}

FaissShard::Result<void> FaissShard::rebuild_id_tracking()
{
    std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
    m_used_ids.clear();
    if (m_index->ntotal == 0)
        return {};
    try
    {
        auto *id_map = dynamic_cast<faiss::IndexIDMap2 *>(m_index.get());
        if (id_map && static_cast<faiss::idx_t>(id_map->id_map.size()) == m_index->ntotal)
        {
            for (faiss::idx_t i = 0; i < m_index->ntotal; ++i)
            {
                m_used_ids.insert(id_map->id_map[i]);
            }
        }
        else
        {
            for (faiss::idx_t i = 0; i < m_index->ntotal; ++i)
            {
                m_used_ids.insert(static_cast<faiss::idx_t>(i));
            }
        }
        return {};
    }
    catch (const std::exception &e)
    {
        return tl::unexpected(
            Error(ErrorType::FAISS_ERROR, "Failed to rebuild shard ID tracking: " + std::string(e.what())));
    }
}

FaissShard::Result<void> FaissShard::load_from_file()
{
    if (m_index_file_path.empty())
    {
        return tl::unexpected(Error(ErrorType::NO_FILE_PATH, "No file path specified for shard"));
    }
    if (!fs::exists(m_index_file_path))
    {
        return tl::unexpected(Error(ErrorType::FILE_NOT_FOUND, "Shard file not found: " + m_index_file_path));
    }
    try
    {
        auto loaded_index = std::unique_ptr<faiss::Index>(faiss::read_index(m_index_file_path.c_str()));
        auto *id_map_ptr = dynamic_cast<faiss::IndexIDMap2 *>(loaded_index.get());
        if (!id_map_ptr)
        {
            return tl::unexpected(Error(ErrorType::INVALID_INDEX_TYPE, "Loaded shard is not of type IndexIDMap2"));
        }
        if (loaded_index->d != m_dimension)
        {
            return tl::unexpected(Error(ErrorType::DIMENSION_MISMATCH_LOADED, "Loaded shard dimension mismatch"));
        }

        std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
        m_index = std::unique_ptr<faiss::IndexIDMap2>(id_map_ptr);
        loaded_index.release();

        lock.unlock();

        auto rebuild_result = rebuild_id_tracking();
        if (!rebuild_result)
            return tl::unexpected(rebuild_result.error());

        return {};
    }
    catch (const std::exception &e)
    {
        return tl::unexpected(Error(ErrorType::FILE_LOAD_ERROR, "Failed to load shard: " + std::string(e.what())));
    }
}
