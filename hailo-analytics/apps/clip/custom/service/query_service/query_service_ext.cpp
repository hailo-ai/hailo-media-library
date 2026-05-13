#include "query_service_ext.hpp"
#include <numeric>

ClipQueryServiceExt::DatabaseConfig::DatabaseConfig(const std::string &faiss, const std::string &thumbnail,
                                                    const std::string &video)
    : faiss_db_name(faiss), thumbnail_db_name(thumbnail), video_db_name(video)
{
}

ClipQueryServiceExt::ThumbQueryResult::ThumbQueryResult(const std::string &jpeg_path, const std::string &description,
                                                        int64_t timestamp, float score)
    : m_jpeg_path(jpeg_path), m_description(description), m_timestamp(timestamp), m_score(score)
{
}

ClipQueryServiceExt::VideoQueryResult::VideoQueryResult(const std::string &video_path, int64_t timestamp_start,
                                                        int64_t timestampend)
    : m_video_path(video_path), m_timestamp_start(timestamp_start), m_timestamp_end(timestampend)
{
    m_duration = m_timestamp_end - m_timestamp_start;
}

ClipQueryServiceExt::PositiveQueryFullRecord::PositiveQueryFullRecord(int64_t faiss_id, int32_t track_id,
                                                                      int64_t timestamp, float score,
                                                                      const std::string &prompt,
                                                                      const std::string &network_embedding_name,
                                                                      const std::string &thumbnail_path,
                                                                      const std::string &classification_label)
    : m_faiss_id(faiss_id), m_track_id(track_id), m_timestamp(timestamp), m_score(score), m_prompt(prompt),
      m_network_embedding_name(network_embedding_name), m_thumbnail_path(thumbnail_path),
      m_classification_label(classification_label)
{
}

void ClipQueryServiceExt::PositiveQueryFullRecord::set_embedding(const std::vector<float> &embedding)
{
    m_embedding = embedding;
}

tl::expected<void, ClipQueryServiceExt::ClipQueryErrCode> ClipQueryServiceExt::configure(
    std::string database_file_path, const DatabaseConfig &db_config, std::shared_ptr<TextEncoder> text_encoder)
{
    if (m_is_configured)
    {
        return {};
    }

    if (!fs::exists(database_file_path))
    {
        return tl::unexpected(ClipQueryErrCode::DB_FILE_NOT_FOUND);
    }

    // Initialize database connections using the provided configuration
    auto faiss_table_result = SqlDatabaseQuickAccess::get_database(db_config.faiss_db_name);
    if (!faiss_table_result)
    {
        return tl::unexpected(ClipQueryErrCode::DATABASE_DOES_NOT_CONTAIN_REQUIRED_TABLE);
    }
    m_faiss_table = std::dynamic_pointer_cast<FaissTable>(faiss_table_result.value());

    auto thumbnail_table_result = SqlDatabaseQuickAccess::get_database(db_config.thumbnail_db_name);
    if (!thumbnail_table_result)
    {
        return tl::unexpected(ClipQueryErrCode::DATABASE_DOES_NOT_CONTAIN_REQUIRED_TABLE);
    }
    m_thumbnail_table = std::dynamic_pointer_cast<ThumbnailTable>(thumbnail_table_result.value());

    auto video_table_result = SqlDatabaseQuickAccess::get_database(db_config.video_db_name);
    if (!video_table_result)
    {
        return tl::unexpected(ClipQueryErrCode::DATABASE_DOES_NOT_CONTAIN_REQUIRED_TABLE);
    }
    m_video_table = std::dynamic_pointer_cast<VideoTable>(video_table_result.value());

    // Initialize the text encoder
    if (!text_encoder)
    {
        return tl::unexpected(ClipQueryErrCode::INVALID_PARAMETER);
    }
    m_text_encoder = text_encoder;

    // Initialize the text encoder if it hasn't been initialized yet
    if (!m_text_encoder->is_initialized())
    {
        auto init_result = m_text_encoder->initialize();
        if (!init_result)
        {
            return tl::unexpected(ClipQueryErrCode::MISSING_CONFIGURATION);
        }
    }

    m_is_configured = true;

    return {};
}

tl::expected<void, ClipQueryServiceExt::ClipQueryErrCode> ClipQueryServiceExt::set_query_video_total_length(
    int64_t length_ms)
{
    if (!m_is_configured)
    {
        return tl::unexpected(ClipQueryErrCode::MISSING_CONFIGURATION);
    }

    // Lets limit the video total length between 10 to 60 seconds
    // please note that this is in milliseconds
    if (length_ms < 10000 || length_ms > 60000)
    {
        return tl::unexpected(ClipQueryErrCode::INVALID_PARAMETER);
    }

    m_video_query_total_length = length_ms;
    return {};
}

tl::expected<int64_t, ClipQueryServiceExt::ClipQueryErrCode> ClipQueryServiceExt::get_query_video_total_length() const
{
    return m_video_query_total_length;
}

tl::expected<std::vector<ClipQueryServiceExt::VideoQueryResult>, ClipQueryServiceExt::ClipQueryErrCode>
ClipQueryServiceExt::query_videos(int64_t timestamp)
{
    if (!m_is_configured)
    {
        return tl::unexpected(ClipQueryErrCode::MISSING_CONFIGURATION);
    }

    if (m_video_table == nullptr)
    {
        return tl::unexpected(ClipQueryErrCode::UNABLE_TO_OPEN_DATABASE);
    }

    int64_t tolerance = m_video_query_total_length / 2; // Use half of the total length as tolerance
    HAILO_ANALYTICS_LOG_INFO("VIDEO_QUERY timestamp={} tolerance={}", timestamp, tolerance);
    auto video_results = m_video_table->query_covering(timestamp, tolerance);
    if (video_results.empty())
    {
        HAILO_ANALYTICS_LOG_INFO("VIDEO_QUERY no results for timestamp={}", timestamp);
        // Just return empty vector if no results found
        return std::vector<VideoQueryResult>();
    }

    std::vector<VideoQueryResult> video_query_results;
    for (const auto &video_info : video_results)
    {
        HAILO_ANALYTICS_LOG_INFO("VIDEO_QUERY_RESULT file={} db_start={} db_end={} "
                                 "offset_from_thumb={}ms",
                                 video_info.path, video_info.start_timestamp, video_info.end_timestamp,
                                 video_info.start_timestamp - timestamp);
        video_query_results.emplace_back(video_info.path, video_info.start_timestamp, video_info.end_timestamp);
    }

    return video_query_results;
}

tl::expected<std::vector<ClipQueryServiceExt::ThumbQueryResult>, ClipQueryServiceExt::ClipQueryErrCode>
ClipQueryServiceExt::query_thumbnails(QueryEmbeddingInfo &query_data)
{
    if (query_data.m_embedding_type == FULLY_ENCODED_TEXT_EMBEDDING)
    {
        // If the embedding type is fully encoded, we can directly query using the provided embeddings
        return query_thumbnails_with_embedding(query_data);
    }
    else
    {
        // If the embedding type requires on-device text encoding, we handle that here
        return query_thumbnails_on_device(query_data);
    }
}

tl::expected<std::vector<ClipQueryServiceExt::ThumbQueryResult>, ClipQueryServiceExt::ClipQueryErrCode>
ClipQueryServiceExt::query_thumbnails_on_device(QueryEmbeddingInfo &query_data)
{
    if (!m_is_configured)
    {
        return tl::unexpected(ClipQueryErrCode::MISSING_CONFIGURATION);
    }

    // Prepare positive and negative prompts
    std::vector<std::string> positive_prompts = {query_data.m_positive_embedding.prompt};
    std::vector<std::string> negative_prompts;

    if (query_data.m_negative_embeddings.empty())
    {
        // If no negative/general prompt provided, use preset negative prompts
        // for automatic prompt selection based on classification label from the top k faiss result.
        TextPromptStore negative_prompt_store;
        negative_prompts = negative_prompt_store.get_all_prompts();
    }
    else
    {
        // Get first negative/general prompt from user input.
        negative_prompts.push_back(query_data.m_negative_embeddings.front().prompt);
    }

    // Encode the text prompts using the text encoder
    auto encoding_result = m_text_encoder->encode_text(query_data.m_network_id, positive_prompts, negative_prompts);
    if (!encoding_result)
    {
        return tl::unexpected(ClipQueryErrCode::TEXT_ENCODING_ERROR);
    }

    const auto &encoder_result = encoding_result.value();

    query_data.m_embedding_type = FULLY_ENCODED_TEXT_EMBEDDING;
    query_data.m_positive_embedding.embedding.clear();
    query_data.m_negative_embeddings.clear();

    // Use the first positive prompt as the main embedding
    if (!encoder_result.positive_embeddings.empty())
    {
        const auto &first_positive = encoder_result.positive_embeddings.begin();
        query_data.m_positive_embedding.prompt = first_positive->first;
        query_data.m_positive_embedding.embedding = first_positive->second;
    }

    // Add negative embeddings
    for (const auto &neg_pair : encoder_result.negative_embeddings)
    {
        QueryEmbeddingInfo::EmbeddingData neg_data;
        neg_data.prompt = neg_pair.first;
        neg_data.embedding = neg_pair.second;
        query_data.m_negative_embeddings.push_back(neg_data);
    }

    // Call the existing query_thumbnails method
    return query_thumbnails_with_embedding(query_data);
}

tl::expected<std::vector<ClipQueryServiceExt::ThumbQueryResult>, ClipQueryServiceExt::ClipQueryErrCode>
ClipQueryServiceExt::query_thumbnails_with_embedding(QueryEmbeddingInfo &query_data)
{
    if (!m_is_configured)
    {
        return tl::unexpected(ClipQueryErrCode::MISSING_CONFIGURATION);
    }

    auto faiss_db_result = FaissDatabaseQuickAccess::get_database(query_data.m_network_id);

    if (!faiss_db_result)
    {
        return tl::unexpected(ClipQueryErrCode::DB_FILE_NOT_FOUND);
    }

    std::vector<ThumbQueryResult> thumbnail_query_results;
    FaissDatabaseQuickAccess::DatabasePtr faiss_db = faiss_db_result.value();
    switch (query_data.m_embedding_type)
    {
    case FULLY_ENCODED_TEXT_EMBEDDING: {
        // Search for top K, we need more result as we will filter them.
        auto top_k_search = query_data.m_max_result * TOP_K_SEARCH_SCALE_FACTOR;

        auto faiss_search_result = faiss_db->search(query_data.m_positive_embedding.embedding, top_k_search);
        if (!FaissDatabaseQuickAccess::handle_faiss_result(faiss_search_result, "Search with top k"))
            return tl::unexpected(ClipQueryErrCode::SEARCH_FAILED);

        HAILO_ANALYTICS_LOG_INFO("Faiss search returned {} results.", faiss_search_result.value().size());

        auto positive_query_results = process_for_positive_query_results(faiss_search_result.value(), query_data);

        // Check if we have any negative embeddings, if not we simply sort and filter the positive results
        if (query_data.m_negative_embeddings.empty())
        {
            auto positive_filtered_results =
                filter_and_sort_by_score(positive_query_results, query_data.m_remove_duplicate_within_sec);
            for (const auto &record : positive_filtered_results)
            {
                thumbnail_query_results.emplace_back(record.m_thumbnail_path, record.m_prompt, record.m_timestamp,
                                                     1.0); // In this case the score is always 1.0

                if (thumbnail_query_results.size() >= query_data.m_max_result)
                    break; // Stop if we reached the max result limit
            }
        }
        else
        {

            // Get embeddings for each record
            for (auto &record : positive_query_results)
            {
                auto embedding_result = faiss_db->get_vector_by_id(record.m_faiss_id);
                if (!embedding_result)
                {
                    HAILO_ANALYTICS_LOG_ERROR(
                        "Error getting embedding for faiss ID: {}, this record will be skipped, Error: {}",
                        record.m_faiss_id, embedding_result.error().message);
                    continue; // Skip this record if embedding retrieval fails
                }
                record.set_embedding(embedding_result.value().vector);
            }

            auto negative_filtered_results = filter_with_negative_embeddings(
                positive_query_results, query_data.m_negative_embeddings, query_data.m_score_threshold);

            auto positive_filtered_results =
                filter_and_sort_by_score(negative_filtered_results, query_data.m_remove_duplicate_within_sec);

            // Add filtered results to thumbnail query results
            for (const auto &record : positive_filtered_results)
            {
                thumbnail_query_results.emplace_back(record.m_thumbnail_path, record.m_prompt, record.m_timestamp,
                                                     record.m_score); // Use the score from the record

                if (thumbnail_query_results.size() >= query_data.m_max_result)
                    break; // Stop if we reached the max result limit
            }
        }

        break;
    }
    default:
        return tl::unexpected(ClipQueryErrCode::UNSUPPORTED_TEXT_EMBEDDING_TYPE);
    }

    return thumbnail_query_results;
}

std::vector<ClipQueryServiceExt::PositiveQueryFullRecord> ClipQueryServiceExt::process_for_positive_query_results(
    std::vector<PartitionedFaissDB::SearchResult> &faiss_index_search_result, QueryEmbeddingInfo &query_data)
{
    // Gather all necessary data and save it to positive_query_results
    std::vector<PositiveQueryFullRecord> positive_query_results;

    // Get all metadata for faiss table batch queries
    std::vector<std::pair<int64_t, std::string>> faiss_batch_queries;
    for (auto faiss_result : faiss_index_search_result)
    {
        faiss_batch_queries.emplace_back(faiss_result.id, query_data.m_network_id);
    }

    // Query faiss table
    auto faiss_table_results = m_faiss_table->query_batch_timestamp(faiss_batch_queries);

    // Get all metadata for thumbnail table batch queries
    std::vector<int64_t> thumb_batch_queries;
    for (const auto &faiss_result : faiss_table_results)
    {
        thumb_batch_queries.emplace_back(faiss_result.result.timestamp);
    }

    // Query thumbnail table
    auto thumbnail_table_results = m_thumbnail_table->query_batch_nearest(thumb_batch_queries);

    // Now we need to combine the results
    for (const auto &thumbnail_result : thumbnail_table_results)
    {
        // Keep the thumbnail data for positive_query_results
        auto thumbnail_path = thumbnail_result.path;

        // Find the corresponding faiss table item from matching timestamp
        auto faiss_table_it =
            std::find_if(faiss_table_results.begin(), faiss_table_results.end(),
                         [&thumbnail_result](const auto &faiss_table_result) {
                             return faiss_table_result.result.timestamp == thumbnail_result.query_timestamp;
                         });

        // Keep the faiss table result
        if (faiss_table_it != faiss_table_results.end())
        {
            // Keep the faiss table data for positive_query_results
            auto track_id = faiss_table_it->result.track_id;
            auto timestamp = faiss_table_it->result.timestamp;
            auto classification_label = faiss_table_it->result.classification_label;

            // Find the corresponding faiss search result item from faiss table faiss_id
            auto faiss_search_it = std::find_if(
                faiss_index_search_result.begin(), faiss_index_search_result.end(),
                [&faiss_table_it](const auto &faiss_result) { return faiss_result.id == faiss_table_it->faiss_id; });

            // Push it to positive_query_results
            if (faiss_search_it != faiss_index_search_result.end())
            {
                // Keep the faiss index data for positive_query_results
                auto faiss_id = faiss_search_it->id;
                auto cosine_sim = faiss_search_it->distance;

                positive_query_results.emplace_back(faiss_id, track_id, timestamp, cosine_sim,
                                                    query_data.m_positive_embedding.prompt, query_data.m_network_id,
                                                    thumbnail_path, classification_label);
            }
        }
    }

    return positive_query_results;
}

std::vector<ClipQueryServiceExt::PositiveQueryFullRecord> ClipQueryServiceExt::filter_with_negative_embeddings(
    std::vector<PositiveQueryFullRecord> records, std::vector<QueryEmbeddingInfo::EmbeddingData> &negative_embeddings,
    float confidence_threshold)
{
    std::vector<PositiveQueryFullRecord> filtered_records;

    for (const auto &record : records)
    {
        if (record.m_embedding.empty())
        {
            continue; // Skip if embedding is empty
        }

        const QueryEmbeddingInfo::EmbeddingData *selected_negative_embedding = &negative_embeddings.front();

        // If we have more than 1 negative embeddings, we will try to find the corresponding prompt
        // and embedding for negative comparison based on classification label
        if (negative_embeddings.size() > 1)
        {
            if (!record.m_classification_label.empty())
            {
                // If classification label is not empty, we will try to find the corresponding prompt and embedding
                // for negative comparison
                auto negative_prompt = TextPromptStore().find_prompt(record.m_classification_label);
                if (negative_prompt.has_value())
                {
                    // If we found the corresponding prompt, we will try to find its embedding from
                    // negative_embeddings
                    auto neg_it = std::find_if(negative_embeddings.begin(), negative_embeddings.end(),
                                               [&negative_prompt](const auto &neg_embedding) {
                                                   return neg_embedding.prompt == negative_prompt.value();
                                               });
                    if (neg_it != negative_embeddings.end())
                    {
                        selected_negative_embedding = &(*neg_it); // Update the pointer to point to the found embedding
                    }
                }
            }
        }

        // Calculate cosine similarity with the selected negative embedding
        std::vector<float> scores{record.m_score};
        calculate_similarities_for_normalized(record.m_embedding, selected_negative_embedding->embedding, scores);

        // Scale scores
        apply_scale(scores, 100.0f);

        // Apply softmax
        apply_softmax(scores);

        // Check if the positive score is above the confidence threshold
        if (scores[0] > confidence_threshold)
        {
            PositiveQueryFullRecord filtered_record = record;
            filtered_record.m_score = scores[0]; // Update the score with the positive score
            filtered_records.push_back(filtered_record);
        }
    }

    return filtered_records;
}

std::vector<ClipQueryServiceExt::PositiveQueryFullRecord> ClipQueryServiceExt::filter_and_sort_by_score(
    std::vector<PositiveQueryFullRecord> records, int timeWindowSeconds)
{
    // 1. Sort the records by m_score in descending order.
    std::sort(records.begin(), records.end(),
              [](const PositiveQueryFullRecord &a, const PositiveQueryFullRecord &b) { return a.m_score > b.m_score; });

    // Convert the time window from seconds to milliseconds for comparison.
    const int64_t timeWindowMillis = timeWindowSeconds * 1000;

    // 2. Filter the records.
    // We will use a nested loop. The outer loop iterates through each record,
    // and the inner loop checks all subsequent (lower-scored) records for duplication.
    // This is an O(n^2) operation, which is acceptable for moderately sized collections.
    for (size_t i = 0; i < records.size(); ++i)
    {
        // The inner loop starts from the element right after the current one.
        // We use an iterator-based or index-based removal pattern.
        for (size_t j = i + 1; j < records.size();)
        {
            // Check for the duplicate condition:
            // - Same m_track_id
            // - Timestamp is within the specified window
            if (records[i].m_track_id == records[j].m_track_id &&
                std::abs(records[i].m_timestamp - records[j].m_timestamp) <= timeWindowMillis)
            {
                // It's a duplicate. Remove the record at index j.
                // `records.erase` returns an iterator to the next element.
                // We don't increment j in this case, because the vector has been
                // shrunk and the element at j+1 is now at the current index j.
                records.erase(records.begin() + j);
            }
            else
            {
                // Not a duplicate, move to the next element.
                ++j;
            }
        }
    }

    return records;
}

float ClipQueryServiceExt::dot_product(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.size() != b.size())
    {
        throw std::invalid_argument("Vectors must be of the same size for dot product.");
    }
    return std::inner_product(a.begin(), a.end(), b.begin(), 0.0f);
}

void ClipQueryServiceExt::calculate_similarities_for_normalized(const std::vector<float> &normalized_image_embedding,
                                                                const std::vector<float> &normalized_text_embedding,
                                                                std::vector<float> &scores)
{
    const float similarity = dot_product(normalized_image_embedding, normalized_text_embedding);
    scores.push_back(similarity);
}

void ClipQueryServiceExt::apply_scale(std::vector<float> &scores, float scale)
{
    for (auto &score : scores)
    {
        score *= scale;
    }
}

void ClipQueryServiceExt::apply_softmax(std::vector<float> &scores)
{
    // Handle empty vector case to avoid errors.
    if (scores.empty())
    {
        return;
    }

    // 1. Find the maximum score for numerical stability.
    // Subtracting the max value from each score before exponentiating prevents
    // `std::exp` from overflowing with large scores. The result is mathematically identical.
    const float max_score = *std::max_element(scores.begin(), scores.end());

    // 2. Exponentiate and calculate the sum.
    float sum_of_exps = 0.0f;
    for (float &score : scores)
    {
        // Apply the stability trick, then exponentiate.
        score = std::exp(score - max_score);
        sum_of_exps += score;
    }

    // 3. Normalize to get the final probabilities.
    // This turns the exponentiated scores into a probability distribution.
    if (sum_of_exps > 0.0f)
    {
        for (float &score : scores)
        {
            score /= sum_of_exps;
        }
    }
    // If sum_of_exps is 0 (e.g., all scores were -infinity), they will remain 0.
}
