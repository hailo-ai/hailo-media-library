#pragma once

#include <tl/expected.hpp>
#include <tokenizers_cpp.h>
#include <sys/types.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <set>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <utility>

#include "text_encoder.hpp" // Include the base class
#include "hailort_service.hpp"

#define TOKEN_START_ID 49406
#define TOKEN_END_ID 49407

using namespace tokenizers;

class TextPromptStore
{
  private:
    std::vector<std::pair<std::string, std::string>> data;

  public:
    TextPromptStore();

    std::vector<std::string> get_all_prompts() const;

    std::optional<std::string> find_prompt(const std::string &key) const;

    bool contains_prompt(const std::string &prompt) const;
};

// Generic struct to load the matrix/vector from your binary format
struct ClipBinMatrix
{
    uint32_t rows;
    uint32_t cols;
    std::vector<float> data; // Stored in row-major order

    bool load(const std::string &path);
    const float *operator[](int token_id) const;
    float at(uint32_t r, uint32_t c) const;
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
                          int emb_dim);
    };

    ClipTextEncoder(const std::vector<TextEncoderConfig> &config, int batch_size = 1);
    ~ClipTextEncoder() override;

    tl::expected<void, ErrorCode> initialize() override;
    tl::expected<EncoderResult, ErrorCode> encode_text(const std::string &network_id,
                                                       const std::vector<std::string> &positive_prompts,
                                                       const std::vector<std::string> &negative_prompts = {}) override;
    bool is_initialized() const override;
    std::vector<std::string> get_supported_networks() const override;

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

    // Cache for negative prompt embeddings
    // Key: (network_id, prompt) -> final projected & normalized embedding
    std::map<std::pair<std::string, std::string>, std::vector<float>> m_embedding_cache;
    TextPromptStore m_prompt_store;

    struct NegativePromptCacheResult
    {
        std::map<std::string, std::vector<float>> cached_embeddings;
        std::vector<std::string> prompts_to_compute;
        std::set<std::string> prompts_to_cache;
    };

    NegativePromptCacheResult resolve_negative_prompt_cache(const std::string &network_id,
                                                            const std::vector<std::string> &negative_prompts);
    std::vector<float> build_sentence_embedding(const std::vector<int> &tokens,
                                                const ClipBinMatrix &embedding_lookup_table, int max_len = 77);
    tl::expected<void, ErrorCode> wait_for_data(int timeout_ms);
    std::vector<float> apply_text_projection(const std::vector<float> &last_hidden_state, const ClipBinMatrix &weights,
                                             const ClipBinMatrix &bias);
    void l2_normalize_inplace(std::vector<float> &vec);
    void save_as_npy(const std::vector<float> &data, int batch_size, int seq_len, int dim, const std::string &filename);
    bool is_prompt_cacheable(const std::string &prompt) const;
};
