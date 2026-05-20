/**
 * Copyright (c) 2026-2027 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "arcface_post.hpp"
#include "common/tensors.hpp"
#include "common/math.hpp"
#include "common/file_reader.hpp"
#include "hailo_postprocess_tools/logger/hailo_postprocess_logger.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_xtensor.hpp"
#include "hailo_postprocess_tools/objects/json_config.hpp"

#include "xtensor/xarray.hpp"
#include "xtensor/xview.hpp"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

#include <fstream>
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <memory>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static constexpr int ARCFACE_EMBEDDING_DIM = 512;
static constexpr float DEFAULT_SIMILARITY_THRESHOLD = 0.6f;
static const std::string R50_OUTPUT_LAYER = "arcface_r50/fc1";
static const std::string MOBILENET_OUTPUT_LAYER = "arcface_mobilefacenet/fc1";
static const std::string DEFAULT_GALLERY_PATH = "/home/root/apps/face_recognition/resources/gallery.json";

// Dot product of two L2-normalized vectors (equivalent to cosine similarity when both are unit-norm).
// Both inputs must be pre-normalized.
static float dot_product_normalized(const std::vector<float> &gallery_embedding, const float *query_embedding,
                                    size_t dim)
{
    if (gallery_embedding.size() != dim)
    {
        return 0.0f;
    }
    float dot_product = 0.0f;
    for (size_t idx = 0; idx < dim; ++idx)
    {
        dot_product += gallery_embedding[idx] * query_embedding[idx];
    }
    return dot_product;
}

static void apply_defaults(ArcfaceParams *params)
{
    params->m_similarity_threshold = DEFAULT_SIMILARITY_THRESHOLD;
    params->m_output_layer = "";
    params->m_gallery_path = DEFAULT_GALLERY_PATH;
}

static void load_config(ArcfaceParams *params, const std::string &config_path)
{
    apply_defaults(params);

    if (config_path.empty())
    {
        HAILO_POSTPROCESS_LOG_WARN("[ArcFace] No config path provided, using default parameters");
        HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Resolved gallery path: {}", params->m_gallery_path);
        return;
    }

    if (!fs::exists(config_path))
    {
        HAILO_POSTPROCESS_LOG_WARN("[ArcFace] Config file not found ({}), using default parameters", config_path);
        HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Resolved gallery path: {}", params->m_gallery_path);
        return;
    }

    static const char *json_schema = R""""({
    "$schema": "http://json-schema.org/draft-04/schema#",
    "type": "object",
    "properties": {
        "similarity_threshold": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
        },
        "output_layer": {
            "type": "string"
        },
        "gallery_path": {
            "type": "string"
        }
    }
    })"""";

    std::string config_content;
    try
    {
        config_content = common::read_file(config_path);
    }
    catch (const std::exception &exception)
    {
        HAILO_POSTPROCESS_LOG_WARN("[ArcFace] Failed to read config {} ({}), using default parameters", config_path,
                                   exception.what());
        HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Resolved gallery path: {}", params->m_gallery_path);
        return;
    }

    try
    {
        common::validate_json_with_schema(config_content, json_schema);
    }
    catch (const std::exception &exception)
    {
        HAILO_POSTPROCESS_LOG_WARN("[ArcFace] Config {} failed schema validation ({}), using default parameters",
                                   config_path, exception.what());
        HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Resolved gallery path: {}", params->m_gallery_path);
        return;
    }

    rapidjson::Document doc;
    doc.Parse(config_content.c_str());

    if (doc.HasMember("similarity_threshold"))
    {
        params->m_similarity_threshold = doc["similarity_threshold"].GetFloat();
    }
    if (doc.HasMember("output_layer"))
    {
        params->m_output_layer = doc["output_layer"].GetString();
    }
    if (doc.HasMember("gallery_path"))
    {
        params->m_gallery_path = doc["gallery_path"].GetString();
    }

    HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Threshold: {}", params->m_similarity_threshold);
    HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Output layer: {}",
                               params->m_output_layer.empty() ? "<auto-detect>" : params->m_output_layer);
    HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Resolved gallery path: {}", params->m_gallery_path);
}

static void load_gallery(ArcfaceParams *params)
{
    std::ifstream file(params->m_gallery_path);
    if (!file.is_open())
    {
        HAILO_POSTPROCESS_LOG_WARN("[ArcFace] Gallery not found at {}, running without gallery",
                                   params->m_gallery_path);
        return;
    }

    try
    {
        nlohmann::json json_data;
        file >> json_data;
        for (const auto &entry : json_data["entries"])
        {
            GalleryEntry gallery_entry;
            gallery_entry.m_name = entry["name"].get<std::string>();
            gallery_entry.m_embeddings = entry["embeddings"].get<std::vector<std::vector<float>>>();
            params->m_gallery.push_back(std::move(gallery_entry));
        }
        int total_embeddings = std::accumulate(
            params->m_gallery.begin(), params->m_gallery.end(), 0,
            [](int sum, const GalleryEntry &entry) { return sum + static_cast<int>(entry.m_embeddings.size()); });
        HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Loaded gallery: {} people, {} total embeddings", params->m_gallery.size(),
                                   total_embeddings);
    }
    catch (const std::exception &exception)
    {
        HAILO_POSTPROCESS_LOG_ERROR("[ArcFace] Failed to parse gallery: {}", exception.what());
    }
}

// ******************************************************************
// INIT / FREE
// ******************************************************************

ArcfaceParams *init(const std::string config_path, const std::string /*function_name*/)
{
    auto params = std::make_unique<ArcfaceParams>();
    load_config(params.get(), config_path);
    load_gallery(params.get());
    return params.release();
}

void free_resources(void *params_void_ptr)
{
    auto *params = static_cast<ArcfaceParams *>(params_void_ptr);
    delete params;
}

// ******************************************************************
// ARCFACE POST-PROCESSING
// ******************************************************************

static void arcface_postprocess(HailoROIPtr roi, const std::string &preferred_layer, const ArcfaceParams *params)
{
    if (!roi->has_tensors())
    {
        return;
    }

    // Get embedding tensor - try preferred layer first, then global config, then auto-detect /fc1
    std::string output_layer = preferred_layer.empty() ? params->m_output_layer : preferred_layer;
    HailoTensorPtr tensor = nullptr;
    try
    {
        tensor = roi->get_tensor(output_layer);
    }
    catch (const std::exception &)
    {
        // Configured layer not found, will try auto-detect below
    }

    if (!tensor)
    {
        for (const auto &candidate_tensor : roi->get_tensors())
        {
            const std::string &tensor_name = candidate_tensor->name();
            if (tensor_name.size() >= 4 && tensor_name.compare(tensor_name.size() - 4, 4, "/fc1") == 0)
            {
                tensor = candidate_tensor;
                output_layer = tensor_name;
                HAILO_POSTPROCESS_LOG_INFO("[ArcFace] Auto-detected output layer: {}", tensor_name);
                break;
            }
        }
    }

    if (!tensor)
    {
        HAILO_POSTPROCESS_LOG_ERROR("[ArcFace] Output tensor not found: {} (and no /fc1 layer detected)", output_layer);
        return;
    }

    // Dequantize
    xt::xarray<float> embedding = common::get_xtensor_float(tensor);

    // L2-normalize
    xt::xarray<float> normalized = common::vector_normalization(embedding);

    // Validate embedding dimension matches expected size
    if (static_cast<int>(normalized.size()) != ARCFACE_EMBEDDING_DIM)
    {
        HAILO_POSTPROCESS_LOG_ERROR("[ArcFace] Embedding dimension mismatch: expected {}, got {}",
                                    ARCFACE_EMBEDDING_DIM, normalized.size());
        return;
    }

    // Store raw embedding as HailoMatrix for downstream use
    std::vector<float> embedding_vec(normalized.data(), normalized.data() + ARCFACE_EMBEDDING_DIM);
    auto matrix = std::make_shared<HailoMatrix>(embedding_vec, 1, ARCFACE_EMBEDDING_DIM);
    roi->add_object(matrix);

    // Match against gallery - max similarity across all embeddings per person
    // Gallery is read-only after init(), no mutex required.
    std::string best_name;
    float best_similarity = 0.0f;

    for (const auto &gallery_entry : params->m_gallery)
    {
        for (const auto &gallery_embedding : gallery_entry.m_embeddings)
        {
            float similarity = dot_product_normalized(gallery_embedding, normalized.data(), ARCFACE_EMBEDDING_DIM);
            if (similarity > best_similarity)
            {
                best_similarity = similarity;
                best_name = gallery_entry.m_name;
            }
        }
    }

    if (!best_name.empty() && best_similarity >= params->m_similarity_threshold)
    {
        roi->add_object(std::make_shared<HailoClassification>("recognition", 1, best_name, best_similarity));
    }
}

void arcface_nv12(HailoROIPtr roi, void *params_void_ptr)
{
    auto *params = static_cast<ArcfaceParams *>(params_void_ptr);
    arcface_postprocess(roi, "", params);
}

void arcface_r50_nv12(HailoROIPtr roi, void *params_void_ptr)
{
    auto *params = static_cast<ArcfaceParams *>(params_void_ptr);
    arcface_postprocess(roi, R50_OUTPUT_LAYER, params);
}

void arcface_mobilenet_nv12(HailoROIPtr roi, void *params_void_ptr)
{
    auto *params = static_cast<ArcfaceParams *>(params_void_ptr);
    arcface_postprocess(roi, MOBILENET_OUTPUT_LAYER, params);
}

void filter(HailoROIPtr roi, void *params_void_ptr)
{
    arcface_nv12(roi, params_void_ptr);
}
