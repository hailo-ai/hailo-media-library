#pragma once

#include <tl/expected.hpp>
#include <faiss/MetricType.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <memory>

#include "include/faiss_table.hpp"
#include "include/thumbnail_table.hpp"
#include "hailo_analytics/analytics/reference_camera_app_constructor.hpp"
#include "faiss_partitioned.hpp"
#include "service/query_service/text_encoder.hpp"
#include "video_table.hpp"

#define TOP_K_SEARCH_SCALE_FACTOR 10

namespace fs = std::filesystem;

class ClipQueryServiceExt : public hailo_analytics::analytics::app_constructor::CameraAppExtension
{
  public:
    struct DatabaseConfig
    {
        std::string faiss_db_name;
        std::string thumbnail_db_name;
        std::string video_db_name;

        DatabaseConfig(const std::string &faiss, const std::string &thumbnail, const std::string &video);
    };

    enum ClipQueryErrCode
    {
        INVALID_PARAMETER,
        MISSING_CONFIGURATION,
        DB_FILE_NOT_FOUND,
        FILE_NOT_FOUND,
        UNABLE_TO_OPEN_DATABASE,
        DATABASE_DOES_NOT_CONTAIN_REQUIRED_TABLE,
        UNSUPPORTED_TEXT_EMBEDDING_TYPE,
        SEARCH_FAILED,
        TEXT_ENCODING_ERROR,
        UNKNOWN_ERROR
    };

    enum EmbeddingVectorType
    {
        FULLY_ENCODED_TEXT_EMBEDDING,
        DEVICE_TO_ENCODE_TEXT_EMBEDDING
    };

    // Store received embeddings for processing
    struct QueryEmbeddingInfo
    {
        struct EmbeddingData
        {
            std::string prompt;           // Text prompt
            std::vector<float> embedding; // Text embedding vector
        };

        std::string m_network_id;             // This is the clip network id that we will be searching against
        EmbeddingVectorType m_embedding_type; // The given text embedding vector data type
        EmbeddingData m_positive_embedding;   // The positive prompt embedding
        std::vector<EmbeddingData> m_negative_embeddings; // For multi-prompt support
        float m_score_threshold = 0.8f;                   // Default threshold
        size_t m_max_result = 10;                         // Maximum return search result
        int m_remove_duplicate_within_sec;                // Remove query duplicate results within X seconds
    };

    struct ThumbQueryResult
    {
        std::string m_jpeg_path;   // JPEG file path
        std::string m_description; // Text description - This is the matching prompt
        int64_t m_timestamp;       // Epoch time in milliseconds
        float m_score;             // The score of this query

        ThumbQueryResult(const std::string &jpeg_path, const std::string &description, int64_t timestamp, float score);
    };

    struct VideoQueryResult
    {

        std::string m_video_path;  // Video file path
        int64_t m_timestamp_start; // Epoch time in milliseconds
        int64_t m_timestamp_end;   // Epoch time in milliseconds
        int64_t m_duration;        // in milliseconds

        VideoQueryResult(const std::string &video_path, int64_t timestamp_start, int64_t timestampend);
    };

    tl::expected<void, ClipQueryErrCode> configure(std::string database_file_path, const DatabaseConfig &db_config,
                                                   std::shared_ptr<TextEncoder> text_encoder);

    tl::expected<void, ClipQueryErrCode> set_query_video_total_length(int64_t length_ms);

    tl::expected<int64_t, ClipQueryErrCode> get_query_video_total_length() const;

    tl::expected<std::vector<VideoQueryResult>, ClipQueryErrCode> query_videos(int64_t timestamp);

    tl::expected<std::vector<ThumbQueryResult>, ClipQueryErrCode> query_thumbnails(QueryEmbeddingInfo &query_data);

    tl::expected<std::vector<ThumbQueryResult>, ClipQueryErrCode> query_thumbnails_on_device(
        QueryEmbeddingInfo &query_data);

    tl::expected<std::vector<ThumbQueryResult>, ClipQueryErrCode> query_thumbnails_with_embedding(
        QueryEmbeddingInfo &query_data);

  private:
    struct PositiveQueryFullRecord
    {
        faiss::idx_t m_faiss_id;
        int32_t m_track_id;
        int64_t m_timestamp;
        float m_score;                  // Cosine similarity score
        std::vector<float> m_embedding; // The embedding vector
        std::string m_prompt;
        std::string m_network_embedding_name;
        std::string m_thumbnail_path;
        std::string m_classification_label;

        PositiveQueryFullRecord(int64_t faiss_id, int32_t track_id, int64_t timestamp, float score,
                                const std::string &prompt, const std::string &network_embedding_name,
                                const std::string &thumbnail_path, const std::string &classification_label = "");

        // Set the embedding vector
        void set_embedding(const std::vector<float> &embedding);
    };

    std::shared_ptr<FaissTable> m_faiss_table;
    std::shared_ptr<ThumbnailTable> m_thumbnail_table;
    std::shared_ptr<VideoTable> m_video_table;
    std::shared_ptr<TextEncoder> m_text_encoder;
    bool m_is_configured = false;
    int64_t m_video_query_total_length = 15 * 1000; // Default total length in milliseconds

    /**
     * @brief   Process the faiss index search result to return with the aggregated data from database tables
     *          to obtain necessary informations such as thumbnail file path
     *
     * @param faiss_index_search_result The faiss index search result to work on
     * @param query_data The query data containing information such as network ID and prompt that is required in
     * aggregated data
     * @return A new vector containing the aggregated data
     */
    std::vector<PositiveQueryFullRecord> process_for_positive_query_results(
        std::vector<PartitionedFaissDB::SearchResult> &faiss_index_search_result, QueryEmbeddingInfo &query_data);

    /**
     * @brief Flter the records with negative embeddings and confidence threshold.
     *
     * @param records The vector of records to be processed. The function works on a copy.
     * @param negative_embeddings A vector of negative embeddings to compare against.
     * @param confidence_threshold The threshold for confidence to keep a record.
     * @return A new vector containing the filtered records.
     */
    std::vector<PositiveQueryFullRecord> filter_with_negative_embeddings(
        std::vector<PositiveQueryFullRecord> records,
        std::vector<QueryEmbeddingInfo::EmbeddingData> &negative_embeddings, float confidence_threshold = 0.8f);

    /**
     * @brief Sorts records by score descending, then filters out duplicates.
     *
     * A duplicate is defined as a record with the same track_id and a timestamp
     * within a specified time window of a higher-scoring record.
     *
     * @param records The vector of records to be processed. The function works on a copy.
     * @param timeWindowSeconds The time window in seconds to check for duplicates.
     * @return A new vector containing the sorted and filtered records.
     */
    std::vector<PositiveQueryFullRecord> filter_and_sort_by_score(std::vector<PositiveQueryFullRecord> records,
                                                                  int timeWindowSeconds = 10);

    /**
     * @brief Calculates the dot product of two vectors.
     * @param a The first vector.
     * @param b The second vector.
     * @return The dot product as a float.
     * @throws std::invalid_argument if the vectors are not of the same size.
     */
    float dot_product(const std::vector<float> &a, const std::vector<float> &b);

    /**
     * @brief Calculates cosine similarities for PRE-NORMALIZED embeddings.
     *
     * The results are appended to the provided 'scores' vector. After similarities
     * is calculated
     *
     * @param normalized_image_embedding A const reference to the NORMALIZED image embedding.
     * @param normalized_text_embedding A const reference to the NORMALIZED text embeddings.
     * @param scores A reference to a vector of floats where the calculated scores will be appended.
     *               This vector is modified in place.
     */
    void calculate_similarities_for_normalized(const std::vector<float> &normalized_image_embedding,
                                               const std::vector<float> &normalized_text_embedding,
                                               std::vector<float> &scores);

    /**
     * @brief Applies scale function to a vector of scores in-place.
     *
     * @param scores A reference to a vector of floats. The vector will be modified
     *               to contain the resulting of scaled scores
     * @param scale The scale factor to apply to each score.
     */
    void apply_scale(std::vector<float> &scores, float scale);

    /**
     * @brief Applies the softmax function to a vector of scores in-place.
     *
     * This function converts a vector of arbitrary real-valued scores into a
     * vector of probabilities that sum to 1. It uses a standard numerical
     * stability trick (subtracting the max score) to prevent overflow with large inputs.
     *
     * @param scores A reference to a vector of floats. The vector will be modified
     *               to contain the resulting softmax probabilities.
     */
    void apply_softmax(std::vector<float> &scores);
};
