#include "clip_text_encoder.hpp"

#include <numeric>
#include <cstring>
#include <set>
#include <algorithm>
#include <cmath>
#include <fstream> // IWYU pragma: keep

#include "media_library/cloexec_fstream.hpp"
#include "clip_pipeline_ai_defines.hpp"
#include "common_utils.hpp"
#include "service/query_service/hailort_service.hpp"
#include "service/query_service/text_encoder.hpp"

ClipTextEncoder::TextEncoderConfig::TextEncoderConfig(const std::string &hailort_device_id,
                                                      const std::string &token_path, const std::string &network_id,
                                                      const std::string &embedding_path, const std::string &weight_path,
                                                      const std::string &bias_path, const std::string &hef_path,
                                                      int emb_dim)
    : hailort_device_id(hailort_device_id), network_id(network_id), token_file_path(token_path),
      embedding_look_up_file_path(embedding_path), projection_weight_file_path(weight_path),
      projection_bias_file_path(bias_path), hef_file_path(hef_path), embedding_dim(emb_dim)
{
}

bool ClipBinMatrix::load(const std::string &path)
{
    cloexec::ifstream f(path, std::ios::binary);
    if (!f)
    {
        HAILO_ANALYTICS_LOG_ERROR("Error: Cannot open file {}", path);
        return false;
    }

    f.read(reinterpret_cast<char *>(&rows), sizeof(uint32_t));
    f.read(reinterpret_cast<char *>(&cols), sizeof(uint32_t));

    data.resize(static_cast<size_t>(rows) * cols);
    f.read(reinterpret_cast<char *>(data.data()), data.size() * sizeof(float));

    if (!f)
    {
        HAILO_ANALYTICS_LOG_ERROR("Error: Failed to read data from {}", path);
        return false;
    }

    HAILO_ANALYTICS_LOG_INFO("Loaded matrix from {} ({} x {})", path, rows, cols);
    return true;
}

const float *ClipBinMatrix::operator[](int token_id) const
{
    if (token_id < 0 || static_cast<uint32_t>(token_id) >= rows)
        return nullptr;
    return &data[token_id * cols];
}

float ClipBinMatrix::at(uint32_t r, uint32_t c) const
{
    return data[r * cols + c];
}

ClipTextEncoder::ClipTextEncoder(const std::vector<TextEncoderConfig> &config, int batch_size)
    : m_config(config), m_batch_size(batch_size), m_initialized(false)
{
}

ClipTextEncoder::~ClipTextEncoder()
{
}

tl::expected<void, TextEncoder::ErrorCode> ClipTextEncoder::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.empty())
    {
        HAILO_ANALYTICS_LOG_ERROR("ClipTextEncoder configuration is empty.");
        return tl::unexpected(ErrorCode::INVALID_PARAMETER);
    }

    for (const auto &cfg : m_config)
    {
        // Initialize HailortService for each network_id
        if (m_hailort_services.find(cfg.network_id) != m_hailort_services.end())
        {
            HAILO_ANALYTICS_LOG_ERROR("Duplicate network_id found in configuration: {}", cfg.network_id);
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }

        auto hailort_service = std::make_shared<HailortService>(cfg.hef_file_path, cfg.hailort_device_id, m_batch_size);
        auto status = hailort_service->initialize();
        if (status != HailortServiceStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to initialize HailortService for network_id: {}", cfg.network_id);
            return tl::unexpected(ErrorCode::SERVICE_ERROR);
        }

        hailort_service->register_output_callback(
            [this, network_id = cfg.network_id](std::vector<float> output_embedding) {
                this->m_output_embeddings.push_back(std::move(output_embedding));

                if (this->m_output_embeddings.size() >= this->m_expected_data_count)
                {
                    {
                        std::lock_guard<std::mutex> lock(this->m_data_mtx);
                        this->m_data_ready = true;
                    }
                    this->cv.notify_one();
                }
            });

        m_hailort_services[cfg.network_id] = hailort_service;

        // Initialize the tokenizer
        std::string json_content = FileSysUtils::read_file(cfg.token_file_path);
        auto tokenizer = Tokenizer::FromBlobJSON(json_content);
        if (!tokenizer)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create tokenizer from file: {}", cfg.token_file_path);
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }

        m_tokenizers[cfg.network_id] = std::move(tokenizer);

        // Load embedding look up table
        ClipBinMatrix embedding_lookup;
        if (!embedding_lookup.load(cfg.embedding_look_up_file_path))
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to load embedding look up from file: {}",
                                      cfg.embedding_look_up_file_path);
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }
        m_embedding_lookup[cfg.network_id] = std::move(embedding_lookup);

        // Load projection weights
        ClipBinMatrix projection_weights;
        if (!projection_weights.load(cfg.projection_weight_file_path))
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to load projection weights from file: {}",
                                      cfg.projection_weight_file_path);
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }
        m_text_projection_weights[cfg.network_id] = std::move(projection_weights);

        // Load projection bias
        ClipBinMatrix projection_bias;
        if (!projection_bias.load(cfg.projection_bias_file_path))
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to load projection bias from file: {}", cfg.projection_bias_file_path);
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }
        m_text_projection_bias[cfg.network_id] = std::move(projection_bias);
    }

    m_initialized = true;
    return {};
}

tl::expected<TextEncoder::EncoderResult, TextEncoder::ErrorCode> ClipTextEncoder::encode_text(
    const std::string &network_id, const std::vector<std::string> &positive_prompts,
    const std::vector<std::string> &negative_prompts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized)
    {
        HAILO_ANALYTICS_LOG_ERROR("ClipTextEncoder not initialized.");
        return tl::unexpected(ErrorCode::UNINITIALIZED);
    }

    // Check if network_id is valid
    if (m_hailort_services.find(network_id) == m_hailort_services.end() ||
        m_tokenizers.find(network_id) == m_tokenizers.end() ||
        m_embedding_lookup.find(network_id) == m_embedding_lookup.end() ||
        m_text_projection_weights.find(network_id) == m_text_projection_weights.end() ||
        m_text_projection_bias.find(network_id) == m_text_projection_bias.end())
    {
        HAILO_ANALYTICS_LOG_ERROR("Invalid network_id or missing configuration: {}", network_id);
        return tl::unexpected(ErrorCode::INVALID_PARAMETER);
    }

    auto tokenizer = m_tokenizers[network_id];
    auto hailort_service = m_hailort_services[network_id];
    const auto &embedding_lookup = m_embedding_lookup[network_id];
    const auto &projection_weights = m_text_projection_weights[network_id];
    const auto &projection_bias = m_text_projection_bias[network_id];

    // Search m_config vector that match network_id and get embedding dimension
    int embedding_dim = 512; // default
    auto it = std::find_if(m_config.begin(), m_config.end(),
                           [&network_id](const TextEncoderConfig &cfg) { return cfg.network_id == network_id; });
    if (it != m_config.end())
    {
        embedding_dim = it->embedding_dim;
    }
    else
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to find configuration for network_id: {}", network_id);
        return tl::unexpected(ErrorCode::INVALID_PARAMETER);
    }

    m_output_embeddings.clear();
    m_data_ready = false;

    auto cache_result = resolve_negative_prompt_cache(network_id, negative_prompts);
    auto &negative_prompts_to_compute = cache_result.prompts_to_compute;
    auto &negative_prompts_to_cache = cache_result.prompts_to_cache;

    m_expected_data_count = positive_prompts.size() + negative_prompts_to_compute.size();

    // Tokenize & embedding lookup & infer positive prompt
    std::map<std::string, int> positive_prompts_eot;
    for (const auto &pos_prompt : positive_prompts)
    {
        auto pos_tokens = tokenizer->Encode(pos_prompt);
        positive_prompts_eot[pos_prompt] = pos_tokens.size() + 1; // +1 for EOT
        auto pos_embedding = build_sentence_embedding(pos_tokens, embedding_lookup);
        auto infer_status = hailort_service->infer(pos_embedding);
        if (infer_status != HailortServiceStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("HailortService inference failed for network_id: {}", network_id);
            return tl::unexpected(ErrorCode::SERVICE_ERROR);
        }
    }

    // Tokenize & embedding lookup negative prompts
    std::map<std::string, int> negative_prompts_eot;
    for (const auto &neg_prompt : negative_prompts_to_compute)
    {
        auto neg_tokens = tokenizer->Encode(neg_prompt);
        negative_prompts_eot[neg_prompt] = neg_tokens.size() + 1; // +1 for EOT
        auto neg_embedding = build_sentence_embedding(neg_tokens, embedding_lookup);
        auto infer_status = hailort_service->infer(neg_embedding);
        if (infer_status != HailortServiceStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("HailortService inference failed for network_id: {}", network_id);
            return tl::unexpected(ErrorCode::SERVICE_ERROR);
        }
    }

    // Wait for all data to be ready with a timeout of 1000 ms (only if there's data to wait for)
    if (m_expected_data_count > 0)
    {
        auto wait_result = wait_for_data(1000);
        if (!wait_result)
        {
            return tl::unexpected(wait_result.error());
        }
    }

    // post-process the results
    std::map<std::string, std::vector<float>> positive_embeddings;
    std::map<std::string, std::vector<float>> negative_embeddings;

    // Start with cached negative embeddings
    negative_embeddings = cache_result.cached_embeddings;

    size_t index = 0;
    for (const auto &pos_prompt : positive_prompts)
    {
        if (index < m_output_embeddings.size())
        {
            auto eot_start_pos = positive_prompts_eot[pos_prompt] * embedding_dim;
            auto eot_end_pos = eot_start_pos + embedding_dim;
            std::vector<float> final_text_embedding(m_output_embeddings[index].begin() + eot_start_pos,
                                                    m_output_embeddings[index].begin() + eot_end_pos);

            auto projected_embedding = apply_text_projection(final_text_embedding, projection_weights, projection_bias);
            l2_normalize_inplace(projected_embedding);
            positive_embeddings[pos_prompt] = std::move(projected_embedding);
            index++;
        }
        else
        {
            HAILO_ANALYTICS_LOG_ERROR("Mismatch in expected positive embeddings count.");
            return tl::unexpected(ErrorCode::ENCODING_ERROR);
        }
    }

    // Process computed negative prompts
    for (const auto &neg_prompt : negative_prompts_to_compute)
    {
        if (index < m_output_embeddings.size())
        {
            auto eot_start_pos = negative_prompts_eot[neg_prompt] * embedding_dim;
            auto eot_end_pos = eot_start_pos + embedding_dim;
            std::vector<float> final_text_embedding(m_output_embeddings[index].begin() + eot_start_pos,
                                                    m_output_embeddings[index].begin() + eot_end_pos);

            auto projected_embedding = apply_text_projection(final_text_embedding, projection_weights, projection_bias);
            l2_normalize_inplace(projected_embedding);

            // Cache the embedding if it's marked for caching
            if (negative_prompts_to_cache.count(neg_prompt) > 0)
            {
                auto cache_key = std::make_pair(network_id, neg_prompt);
                m_embedding_cache[cache_key] = projected_embedding;
                HAILO_ANALYTICS_LOG_DEBUG("Cached negative prompt embedding: {}", neg_prompt);
            }

            negative_embeddings[neg_prompt] = std::move(projected_embedding);
            index++;
        }
        else
        {
            HAILO_ANALYTICS_LOG_ERROR("Mismatch in expected negative embeddings count.");
            return tl::unexpected(ErrorCode::ENCODING_ERROR);
        }
    }

    return EncoderResult(network_id, std::move(positive_embeddings), std::move(negative_embeddings));
}

bool ClipTextEncoder::is_initialized() const
{
    return m_initialized;
}

std::vector<std::string> ClipTextEncoder::get_supported_networks() const
{
    std::vector<std::string> network_ids;
    network_ids.reserve(m_config.size());
    for (const auto &cfg : m_config)
    {
        network_ids.push_back(cfg.network_id);
    }
    return network_ids;
}

std::vector<float> ClipTextEncoder::build_sentence_embedding(const std::vector<int> &tokens,
                                                             const ClipBinMatrix &embedding_lookup_table, int max_len)
{
    std::vector<int> token_ids;
    token_ids.reserve(max_len);

    token_ids.push_back(TOKEN_START_ID);

    for (int t : tokens)
    {
        if ((int)token_ids.size() >= max_len - 1)
            break;
        token_ids.push_back(t);
    }

    token_ids.push_back(TOKEN_END_ID);

    while ((int)token_ids.size() < max_len)
    {
        token_ids.push_back(0);
    }

    std::vector<float> tensor;
    tensor.resize(max_len * embedding_lookup_table.cols);

    for (int i = 0; i < max_len; i++)
    {
        const float *emb = embedding_lookup_table[token_ids[i]];
        if (!emb)
            continue;
        std::memcpy(&tensor[i * embedding_lookup_table.cols], emb, embedding_lookup_table.cols * sizeof(float));
    }

    return tensor;
}

tl::expected<void, TextEncoder::ErrorCode> ClipTextEncoder::wait_for_data(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_data_mtx);
    bool success = cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return m_data_ready.load(); });

    if (!success)
    {
        HAILO_ANALYTICS_LOG_INFO("Timeout! Data was not ready in {} ms.", timeout_ms);
        return tl::unexpected(ErrorCode::TIMEOUT);
    }
    return {};
}

std::vector<float> ClipTextEncoder::apply_text_projection(const std::vector<float> &last_hidden_state,
                                                          const ClipBinMatrix &weights, const ClipBinMatrix &bias)
{
    const uint32_t output_dim = weights.cols;
    const uint32_t input_dim = weights.rows;
    std::vector<float> projected_embedding(output_dim);

    for (uint32_t i = 0; i < output_dim; ++i)
    {
        float sum = 0.0f;
        for (uint32_t j = 0; j < input_dim; ++j)
        {
            sum += last_hidden_state[j] * weights.at(j, i);
        }
        projected_embedding[i] = sum;
    }

    for (uint32_t i = 0; i < output_dim; ++i)
    {
        projected_embedding[i] += bias.data[i];
    }

    return projected_embedding;
}

void ClipTextEncoder::l2_normalize_inplace(std::vector<float> &vec)
{
    float norm = std::sqrt(std::inner_product(vec.begin(), vec.end(), vec.begin(), 0.0f));
    if (norm == 0.0f)
        return;
    for (float &val : vec)
    {
        val /= norm;
    }
}

void ClipTextEncoder::save_as_npy(const std::vector<float> &data, int batch_size, int seq_len, int dim,
                                  const std::string &filename)
{
    cloexec::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    file.write("\x93NUMPY", 6);
    file.put(0x01);
    file.put(0x00);

    std::ostringstream header_stream;
    header_stream << "{'descr': '<f4', 'fortran_order': False, 'shape': (" << batch_size << ", " << seq_len << ", "
                  << dim << "), }";

    std::string header = header_stream.str();

    size_t header_len = header.size() + 1;
    size_t padding = 16 - ((10 + header_len) % 16);
    header.append(padding, ' ');
    header += '\n';

    uint16_t header_size = static_cast<uint16_t>(header.size());
    file.write(reinterpret_cast<const char *>(&header_size), sizeof(header_size));

    file.write(header.c_str(), header.size());

    file.write(reinterpret_cast<const char *>(data.data()), data.size() * sizeof(float));
}

// TextPromptStore implementation
TextPromptStore::TextPromptStore()
{
    data = {
        {app::classes::clip_crop_target_label_person, "a photo of a person"},
        {app::classes::clip_crop_target_label_vehicle, "a photo of a vehicle"},
        {app::classes::clip_crop_target_label_face, "a photo of a face"},
        {app::classes::clip_crop_target_label_scene, "a photo of a scene"} // Add more pairs as needed
    };
}

std::vector<std::string> TextPromptStore::get_all_prompts() const
{
    std::vector<std::string> prompts;
    prompts.reserve(data.size());
    for (const auto &pair : data)
    {
        prompts.push_back(pair.second);
    }
    return prompts;
}

std::optional<std::string> TextPromptStore::find_prompt(const std::string &key) const
{
    for (const auto &pair : data)
    {
        if (pair.first == key)
        {
            return pair.second;
        }
    }
    return std::nullopt;
}

bool TextPromptStore::contains_prompt(const std::string &prompt) const
{
    return find_prompt(prompt) != std::nullopt;
}

ClipTextEncoder::NegativePromptCacheResult ClipTextEncoder::resolve_negative_prompt_cache(
    const std::string &network_id, const std::vector<std::string> &negative_prompts)
{
    NegativePromptCacheResult result;

    for (const auto &neg_prompt : negative_prompts)
    {
        auto cache_key = std::make_pair(network_id, neg_prompt);
        auto cache_it = m_embedding_cache.find(cache_key);

        if (cache_it != m_embedding_cache.end())
        {
            result.cached_embeddings[neg_prompt] = cache_it->second;
            HAILO_ANALYTICS_LOG_DEBUG("Cache hit for negative prompt: {}", neg_prompt);
        }
        else
        {
            result.prompts_to_compute.push_back(neg_prompt);

            if (is_prompt_cacheable(neg_prompt))
            {
                result.prompts_to_cache.insert(neg_prompt);
                HAILO_ANALYTICS_LOG_DEBUG("Negative prompt marked for caching: {}", neg_prompt);
            }
        }
    }

    return result;
}

bool ClipTextEncoder::is_prompt_cacheable(const std::string &prompt) const
{
    return m_prompt_store.contains_prompt(prompt);
}
