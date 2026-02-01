#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <thread>
#include <map>
#include <tl/expected.hpp>
#include <tokenizers_cpp.h>

#include "text_encoder.hpp" // Include the base class
#include "hailort_service.hpp"
#include "common_utils.hpp"

#define TOKEN_START_ID 49406
#define TOKEN_END_ID 49407

using namespace tokenizers;

// Generic struct to load the matrix/vector from your binary format
struct ClipBinMatrix
{
    uint32_t rows;
    uint32_t cols;
    std::vector<float> data; // Stored in row-major order

    bool load(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            std::cerr << "Error: Cannot open file " << path << std::endl;
            return false;
        }

        f.read(reinterpret_cast<char *>(&rows), sizeof(uint32_t));
        f.read(reinterpret_cast<char *>(&cols), sizeof(uint32_t));

        data.resize(static_cast<size_t>(rows) * cols);
        f.read(reinterpret_cast<char *>(data.data()), data.size() * sizeof(float));

        if (!f)
        {
            std::cerr << "Error: Failed to read data from " << path << std::endl;
            return false;
        }

        std::cout << "Loaded matrix from " << path << " (" << rows << " x " << cols << ")" << std::endl;
        return true;
    }

    // Lookup one token ID -> pointer to embedding vector
    const float *operator[](int token_id) const
    {
        if (token_id < 0 || static_cast<uint32_t>(token_id) >= rows)
            return nullptr;
        return &data[token_id * cols];
    }

    // Access element at (r, c)
    float at(uint32_t r, uint32_t c) const
    {
        return data[r * cols + c];
    }
};

class ClipTextEncoder : public TextEncoder
{
  public:
    struct TextEncoderConfig
    {
        std::string hailort_device_id;
        std::string network_id;
        std::string token_file_path;
        std::string embedding_look_up_file_path;
        std::string projection_weight_file_path;
        std::string projection_bias_file_path;
        std::string hef_file_path;
        int embedding_dim;

        TextEncoderConfig(const std::string &hailort_device_id, const std::string &token_path,
                          const std::string &network_id, const std::string &embedding_path,
                          const std::string &weight_path, const std::string &bias_path, const std::string &hef_path,
                          int emb_dim)
            : hailort_device_id(hailort_device_id), network_id(network_id), token_file_path(token_path),
              embedding_look_up_file_path(embedding_path), projection_weight_file_path(weight_path),
              projection_bias_file_path(bias_path), hef_file_path(hef_path), embedding_dim(emb_dim)
        {
        }
    };

    ClipTextEncoder(const std::vector<TextEncoderConfig> &config, int batch_size = 1)
        : m_config(config), m_batch_size(batch_size), m_initialized(false), m_thread_running(false) {};

    ~ClipTextEncoder() override
    {
        stop_worker_thread();
    }

    // Override virtual functions from base class
    tl::expected<void, ErrorCode> initialize() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config.empty())
        {
            std::cerr << "ClipTextEncoder configuration is empty." << std::endl;
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }

#if 1 // AARON DEBUG - REMOVE TO DEBUG ONLY, SHOULD BE ENABLED!!
        for (const auto &cfg : m_config)
        {
            // Initialize HailortService for each network_id
            if (m_hailort_services.find(cfg.network_id) != m_hailort_services.end())
            {
                std::cerr << "Duplicate network_id found in configuration: " << cfg.network_id << std::endl;
                return tl::unexpected(ErrorCode::INVALID_PARAMETER);
            }

            auto hailort_service =
                std::make_shared<HailortService>(cfg.hef_file_path, cfg.hailort_device_id, m_batch_size);
            auto status = hailort_service->initialize();
            if (status != HailortServiceStatus::SUCCESS)
            {
                std::cerr << "Failed to initialize HailortService for network_id: " << cfg.network_id << std::endl;
                return tl::unexpected(ErrorCode::SERVICE_ERROR);
            }

            // We can safely capture this because the lambda will not outlive the ClipTextEncoder instance
            // since text_encoder is a blocking call
            hailort_service->register_output_callback(
                [this, network_id = cfg.network_id](std::vector<float> output_embedding) {
                    std::cout << "Received h15 text decoded embedding from network_id: " << network_id << std::endl;

                    // AARON DEBUG ONLY
                    // save_as_npy(output_embedding, 1, 77, 512, network_id +
                    // "_output_embedding_before_projection.npy");

                    // Store the output
                    this->m_output_embeddings.push_back(std::move(output_embedding));

                    // Notify waiting thread if we have received all expected data
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

            // Initialize the tokenize
            std::string json_content = FileSysUtils::read_file(cfg.token_file_path);
            auto tokenizer = Tokenizer::FromBlobJSON(json_content);
            if (!tokenizer)
            {
                std::cerr << "Failed to create tokenizer from file: " << cfg.token_file_path << std::endl;
                return tl::unexpected(ErrorCode::INVALID_PARAMETER);
            }

            m_tokenizers[cfg.network_id] = std::move(tokenizer);

            // Load embedding look up table
            ClipBinMatrix embedding_lookup;
            if (!embedding_lookup.load(cfg.embedding_look_up_file_path))
            {
                std::cerr << "Failed to load embedding look up from file: " << cfg.embedding_look_up_file_path
                          << std::endl;
                return tl::unexpected(ErrorCode::INVALID_PARAMETER);
            }
            m_embedding_lookup[cfg.network_id] = std::move(embedding_lookup);

            // Load projection weights
            ClipBinMatrix projection_weights;
            if (!projection_weights.load(cfg.projection_weight_file_path))
            {
                std::cerr << "Failed to load projection weights from file: " << cfg.projection_weight_file_path
                          << std::endl;
                return tl::unexpected(ErrorCode::INVALID_PARAMETER);
            }
            m_text_projection_weights[cfg.network_id] = std::move(projection_weights);

            // Load projection bias
            ClipBinMatrix projection_bias;
            if (!projection_bias.load(cfg.projection_bias_file_path))
            {
                std::cerr << "Failed to load projection bias from file: " << cfg.projection_bias_file_path << std::endl;
                return tl::unexpected(ErrorCode::INVALID_PARAMETER);
            }
            m_text_projection_bias[cfg.network_id] = std::move(projection_bias);
        }

        // start_worker_thread();
#endif

        m_initialized = true;
        return {};
    }

    tl::expected<EncoderResult, ErrorCode> encode_text(const std::string &network_id,
                                                       const std::vector<std::string> &positive_prompts,
                                                       const std::vector<std::string> &negative_prompts = {}) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_initialized)
        {
            std::cerr << "ClipTextEncoder not initialized." << std::endl;
            return tl::unexpected(ErrorCode::UNINITIALIZED);
        }

        // Check if network_id is valid
        if (m_hailort_services.find(network_id) == m_hailort_services.end() ||
            m_tokenizers.find(network_id) == m_tokenizers.end() ||
            m_embedding_lookup.find(network_id) == m_embedding_lookup.end() ||
            m_text_projection_weights.find(network_id) == m_text_projection_weights.end() ||
            m_text_projection_bias.find(network_id) == m_text_projection_bias.end())
        {
            std::cerr << "Invalid network_id or missing configuration: " << network_id << std::endl;
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
            std::cerr << "Failed to find configuration for network_id: " << network_id << std::endl;
            return tl::unexpected(ErrorCode::INVALID_PARAMETER);
        }

        m_output_embeddings.clear();
        m_data_ready = false;
        m_expected_data_count = positive_prompts.size() + negative_prompts.size();

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
                std::cerr << "HailortService inference failed for network_id: " << network_id << std::endl;
                return tl::unexpected(ErrorCode::SERVICE_ERROR);
            }
        }

        // Tokenize & embedding lookup negative prompts
        std::map<std::string, int> negative_prompts_eot;
        for (const auto &neg_prompt : negative_prompts)
        {
            auto neg_tokens = tokenizer->Encode(neg_prompt);
            negative_prompts_eot[neg_prompt] = neg_tokens.size() + 1; // +1 for EOT
            auto neg_embedding = build_sentence_embedding(neg_tokens, embedding_lookup);
            auto infer_status = hailort_service->infer(neg_embedding);
            if (infer_status != HailortServiceStatus::SUCCESS)
            {
                std::cerr << "HailortService inference failed for network_id: " << network_id << std::endl;
                return tl::unexpected(ErrorCode::SERVICE_ERROR);
            }
        }

        // Wait for all data to be ready with a timeout of 1000 ms
        auto wait_result = wait_for_data(1000);
        if (!wait_result)
        {
            return tl::unexpected(wait_result.error());
        }

        // post-process the results
        std::map<std::string, std::vector<float>> positive_embeddings;
        std::map<std::string, std::vector<float>> negative_embeddings;
        size_t index = 0;
        for (const auto &pos_prompt : positive_prompts)
        {
            if (index < m_output_embeddings.size())
            {
                // Get the EOT text embedding
                auto eot_start_pos = positive_prompts_eot[pos_prompt] * embedding_dim;
                auto eot_end_pos = eot_start_pos + embedding_dim;
                std::vector<float> final_text_embedding(m_output_embeddings[index].begin() + eot_start_pos,
                                                        m_output_embeddings[index].begin() + eot_end_pos);

                // Apply text projection
                auto projected_embedding =
                    apply_text_projection(final_text_embedding, projection_weights, projection_bias);
                // L2 normalize
                l2_normalize_inplace(projected_embedding);
                positive_embeddings[pos_prompt] = std::move(projected_embedding);
                index++;
            }
            else
            {
                std::cerr << "Mismatch in expected positive embeddings count." << std::endl;
                return tl::unexpected(ErrorCode::ENCODING_ERROR);
            }
        }

        for (const auto &neg_prompt : negative_prompts)
        {
            if (index < m_output_embeddings.size())
            {
                // Get the EOT text embedding
                auto eot_start_pos = negative_prompts_eot[neg_prompt] * embedding_dim;
                auto eot_end_pos = eot_start_pos + embedding_dim;
                std::vector<float> final_text_embedding(m_output_embeddings[index].begin() + eot_start_pos,
                                                        m_output_embeddings[index].begin() + eot_end_pos);

                // Apply text projection
                auto projected_embedding =
                    apply_text_projection(final_text_embedding, projection_weights, projection_bias);
                // L2 normalize
                l2_normalize_inplace(projected_embedding);
                negative_embeddings[neg_prompt] = std::move(projected_embedding);
                index++;
            }
            else
            {
                std::cerr << "Mismatch in expected negative embeddings count." << std::endl;
                return tl::unexpected(ErrorCode::ENCODING_ERROR);
            }
        }

        return EncoderResult(network_id, std::move(positive_embeddings), std::move(negative_embeddings));
    }

    // Additional virtual function implementations from base class
    bool is_initialized() const override
    {
        return m_initialized;
    }

    std::vector<std::string> get_supported_networks() const override
    {
        std::vector<std::string> network_ids;
        network_ids.reserve(m_config.size());
        for (const auto &cfg : m_config)
        {
            network_ids.push_back(cfg.network_id);
        }
        return network_ids;
    }

    // Thread management functions
    void start_worker_thread()
    {
        if (!m_thread_running)
        {
            m_thread_running = true;
            m_worker_thread = std::thread(&ClipTextEncoder::worker_thread_function, this);
        }
    }

    void stop_worker_thread()
    {
        if (m_thread_running)
        {
            m_thread_running = false;
            m_thread_cv.notify_all();
            if (m_worker_thread.joinable())
            {
                m_worker_thread.join();
            }
        }
    }

  private:
    std::mutex m_mutex;
    std::vector<TextEncoderConfig> m_config;
    int m_batch_size;
    bool m_initialized;
    std::map<std::string, std::shared_ptr<HailortService>> m_hailort_services;
    std::map<std::string, std::shared_ptr<Tokenizer>> m_tokenizers;
    std::map<std::string, ClipBinMatrix> m_embedding_lookup;
    std::map<std::string, ClipBinMatrix> m_text_projection_weights;
    std::map<std::string, ClipBinMatrix> m_text_projection_bias;
    std::vector<std::vector<float>> m_output_embeddings;

    std::condition_variable cv;
    std::mutex m_data_mtx;
    std::atomic<bool> m_data_ready = false;
    std::atomic<uint> m_expected_data_count = 0;

    // Thread-related members
    // AARON TODO:   THIS is for TEST ONLY to workaround the possible hailort timeout issue when instantiated
    //              hailort service is not being used for a while (eg few minutes) which cause application pipeline
    //              FPS drop to 3 FPS (although actual root cause is unknown yet but seems related to hailort)
    std::thread m_worker_thread;
    std::atomic<bool> m_thread_running = false;
    std::condition_variable m_thread_cv;
    std::mutex m_thread_mutex;

    std::vector<float> build_sentence_embedding(const std::vector<int> &tokens,
                                                const ClipBinMatrix &embedding_lookup_table, int max_len = 77)
    {
        std::vector<int> token_ids;
        token_ids.reserve(max_len);

        // Add start token
        token_ids.push_back(TOKEN_START_ID);

        // Add sentence tokens
        for (int t : tokens)
        {
            if ((int)token_ids.size() >= max_len - 1)
                break; // leave space for end token
            token_ids.push_back(t);
        }

        // Add end token
        token_ids.push_back(TOKEN_END_ID);

        // Pad with 0 or EOT (49407)? if shorter than max_len
        while ((int)token_ids.size() < max_len)
        {
            token_ids.push_back(0);
        }

        // Build tensor [1 x max_len x dim]
        std::vector<float> tensor;
        tensor.resize(max_len * embedding_lookup_table.cols);

        for (int i = 0; i < max_len; i++)
        {
            const float *emb = embedding_lookup_table[token_ids[i]];
            if (!emb)
                continue;
            std::memcpy(&tensor[i * embedding_lookup_table.cols], emb, embedding_lookup_table.cols * sizeof(float));
        }

        return tensor; // row-major [77 x dim]
    }

    // Function waits for data with a timeout in milliseconds
    tl::expected<void, ErrorCode> wait_for_data(int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_data_mtx);
        bool success = cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return m_data_ready.load(); });

        if (!success)
        {
            std::cout << "Timeout! Data was not ready in " << timeout_ms << " ms." << std::endl;
            return tl::unexpected(ErrorCode::TIMEOUT);
        }
        return {};
    }

    // Function to perform the text projection: result = input @ weights + bias
    std::vector<float> apply_text_projection(const std::vector<float> &last_hidden_state, const ClipBinMatrix &weights,
                                             const ClipBinMatrix &bias)
    {
        const uint32_t output_dim = weights.cols;
        const uint32_t input_dim = weights.rows;
        std::vector<float> projected_embedding(output_dim);

        // 1. Matrix-Vector Multiplication: projected_embedding = input @ weights
        // weights is [input_dim, output_dim], input is [input_dim]
        for (uint32_t i = 0; i < output_dim; ++i)
        {
            float sum = 0.0f;
            for (uint32_t j = 0; j < input_dim; ++j)
            {
                // Dot product: sum over input_dim of input[j] * weights[j][i]
                sum += last_hidden_state[j] * weights.at(j, i);
            }
            projected_embedding[i] = sum;
        }

        // 2. Add Bias: projected_embedding = projected_embedding + bias
        for (uint32_t i = 0; i < output_dim; ++i)
        {
            projected_embedding[i] += bias.data[i];
        }

        return projected_embedding;
    }

    // L2 normalization function
    void l2_normalize_inplace(std::vector<float> &vec)
    {
        float norm = std::sqrt(std::inner_product(vec.begin(), vec.end(), vec.begin(), 0.0f));
        if (norm == 0.0f)
            return;
        for (float &val : vec)
        {
            val /= norm;
        }
    }

    void save_as_npy(const std::vector<float> &data, int batch_size, int seq_len, int dim, const std::string &filename)
    {
        std::ofstream file(filename, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        // Magic string and version
        file.write("\x93NUMPY", 6);
        file.put(0x01); // major version
        file.put(0x00); // minor version

        // Construct header dictionary
        std::ostringstream header_stream;
        header_stream << "{'descr': '<f4', 'fortran_order': False, 'shape': (" << batch_size << ", " << seq_len << ", "
                      << dim << "), }";

        std::string header = header_stream.str();

        // Pad the header to make total header length divisible by 16
        size_t header_len = header.size() + 1;          // +1 for newline
        size_t padding = 16 - ((10 + header_len) % 16); // 10 bytes for magic + version + header_len
        header.append(padding, ' ');
        header += '\n';

        // Write header length
        uint16_t header_size = static_cast<uint16_t>(header.size());
        file.write(reinterpret_cast<const char *>(&header_size), sizeof(header_size));

        // Write header
        file.write(header.c_str(), header.size());

        // Write data
        file.write(reinterpret_cast<const char *>(data.data()), data.size() * sizeof(float));
    }

    // Worker thread function template - you can implement your logic here
    void worker_thread_function()
    {
        std::cout << "Worker thread started" << std::endl;

        sleep(5);

        while (m_thread_running)
        {
            // Wait for work or shutdown signal
            std::unique_lock<std::mutex> lock(m_thread_mutex);
            m_thread_cv.wait_for(lock, std::chrono::seconds(1), [this]() { return !m_thread_running; });

            if (!m_thread_running)
            {
                break;
            }

            perform_background_task();
        }

        std::cout << "Worker thread stopped" << std::endl;
    }

    // Template function for your custom background tasks
    void perform_background_task()
    {
        // Example implementation - customize this for your needs
        if (!m_initialized)
        {
            return;
        }

        for (const auto &[network_id, service] : m_hailort_services)
        {
            encode_text(network_id, std::vector<std::string>({"a photo of a cat"}));
            std::cout << "Network: " << network_id << " text encode performed to keep Hailort alive" << std::endl;
        }
    }
};
