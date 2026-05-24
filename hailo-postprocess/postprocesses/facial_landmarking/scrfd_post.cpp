/**
 * Copyright (c) 2026-2027 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/

#include "scrfd_post.hpp"
#include "common/tensors.hpp"
#include "common/nms.hpp"
#include "common/file_reader.hpp"
#include "hailo_postprocess_tools/logger/hailo_postprocess_logger.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_xtensor.hpp"

#include "xtensor/xarray.hpp"
#include "xtensor/xview.hpp"
#include "xtensor/xio.hpp"
#include "xtensor/xpad.hpp"
#include "xtensor/xsort.hpp"
#include "xtensor/xadapt.hpp"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// Constants
// ============================================================================

static constexpr int COORDS_PER_BOX = 4;
static constexpr int LANDMARKS_PER_FACE = 5;
static constexpr int COORDS_PER_LANDMARK = 2;
static constexpr int LANDMARK_VALUES_PER_ANCHOR = LANDMARKS_PER_FACE * COORDS_PER_LANDMARK; // 10
static constexpr int ANCHORS_PER_LOCATION = 2;

// Layer names for scrfd_10g (from the actual HEF)
static const std::vector<std::string> BOXES_10G = {"scrfd_10g_384_640/conv42", "scrfd_10g_384_640/conv50",
                                                   "scrfd_10g_384_640/conv57"};

static const std::vector<std::string> CLASSES_10G = {"scrfd_10g_384_640/conv41", "scrfd_10g_384_640/conv49",
                                                     "scrfd_10g_384_640/conv56"};

static const std::vector<std::string> LANDMARKS_10G = {"scrfd_10g_384_640/conv43", "scrfd_10g_384_640/conv51",
                                                       "scrfd_10g_384_640/conv58"};

// ============================================================================
// Anchor Generation
// ============================================================================

/**
 * @brief Generate prior anchor boxes for all SCRFD output scales.
 *
 * For each stride level, creates a grid of anchor centers and scales.
 * Each location has multiple anchors (one per min_size entry).
 * Anchors are stored as [center_y, center_x, scale_y, scale_x] normalized
 * to the image dimensions.
 */
static xt::xarray<float> generate_anchors(const std::vector<std::vector<int>> &anchor_min_sizes,
                                          const std::vector<int> &anchor_steps, int image_width, int image_height)
{
    // Count total anchors across all scales
    int total_anchors = 0;
    for (size_t index = 0; index < anchor_min_sizes.size(); ++index)
    {
        int grid_width = image_width / anchor_steps[index];
        int grid_height = image_height / anchor_steps[index];
        int num_anchors_per_cell = static_cast<int>(anchor_min_sizes[index].size());
        total_anchors += grid_width * grid_height * num_anchors_per_cell;
    }

    // Each anchor: [center_x, center_y, scale_x, scale_y]
    // This order matches decode_boxes → encode_detections which treats
    // column 0 as x and column 1 as y.
    xt::xarray<float> anchors = xt::zeros<float>({total_anchors, 4});
    int anchor_offset = 0;

    for (size_t index = 0; index < anchor_min_sizes.size(); ++index)
    {
        int step = anchor_steps[index];
        int grid_width = image_width / step;
        int grid_height = image_height / step;
        int num_anchors_per_cell = static_cast<int>(anchor_min_sizes[index].size());
        float scale_x = static_cast<float>(step) / static_cast<float>(image_width);
        float scale_y = static_cast<float>(step) / static_cast<float>(image_height);

        for (int row = 0; row < grid_height; ++row)
        {
            for (int col = 0; col < grid_width; ++col)
            {
                float center_x = static_cast<float>(col * step) / static_cast<float>(image_width);
                float center_y = static_cast<float>(row * step) / static_cast<float>(image_height);

                for (int anchor = 0; anchor < num_anchors_per_cell; ++anchor)
                {
                    anchors(anchor_offset, 0) = center_x;
                    anchors(anchor_offset, 1) = center_y;
                    anchors(anchor_offset, 2) = scale_x;
                    anchors(anchor_offset, 3) = scale_y;
                    ++anchor_offset;
                }
            }
        }
    }

    return anchors;
}

// ============================================================================
// Box and Landmark Decoding
// ============================================================================

/**
 * @brief Decode bounding boxes from anchor-relative offsets.
 *
 * Formula:
 *   xmin = anchor_cx - offset_left  * anchor_scale_x
 *   ymin = anchor_cy - offset_top   * anchor_scale_y
 *   xmax = anchor_cx + offset_right * anchor_scale_x
 *   ymax = anchor_cy + offset_bottom* anchor_scale_y
 */
static xt::xarray<float> decode_boxes(const xt::xarray<float> &box_offsets, const xt::xarray<float> &anchors)
{
    xt::xarray<float> boxes = xt::zeros<float>(box_offsets.shape());
    xt::col(boxes, 0) = xt::col(anchors, 0) - (xt::col(box_offsets, 0) * xt::col(anchors, 2));
    xt::col(boxes, 1) = xt::col(anchors, 1) - (xt::col(box_offsets, 1) * xt::col(anchors, 3));
    xt::col(boxes, 2) = xt::col(anchors, 0) + (xt::col(box_offsets, 2) * xt::col(anchors, 2));
    xt::col(boxes, 3) = xt::col(anchors, 1) + (xt::col(box_offsets, 3) * xt::col(anchors, 3));
    return boxes;
}

/**
 * @brief Decode facial landmarks from anchor-relative offsets.
 *
 * Each landmark (x, y) is decoded as: anchor_center + offset * anchor_scale.
 */
static xt::xarray<float> decode_landmarks(const xt::xarray<float> &landmark_offsets, const xt::xarray<float> &anchors)
{
    int num_detections = static_cast<int>(landmark_offsets.shape(0));
    xt::xarray<float> landmarks = xt::zeros<float>({num_detections, LANDMARK_VALUES_PER_ANCHOR});

    for (int det = 0; det < num_detections; ++det)
    {
        float center_x = anchors(det, 0);
        float center_y = anchors(det, 1);
        float scale_x = anchors(det, 2);
        float scale_y = anchors(det, 3);

        for (int lm = 0; lm < LANDMARKS_PER_FACE; ++lm)
        {
            int x_idx = lm * COORDS_PER_LANDMARK;
            int y_idx = lm * COORDS_PER_LANDMARK + 1;
            landmarks(det, x_idx) = center_x + landmark_offsets(det, x_idx) * scale_x;
            landmarks(det, y_idx) = center_y + landmark_offsets(det, y_idx) * scale_y;
        }
    }

    return landmarks;
}

// ============================================================================
// Per-Branch Decoding
// ============================================================================

/**
 * @brief Decode a single output scale branch: filter by score threshold,
 *        dequantize, and decode boxes + landmarks.
 *
 * @param tensors_by_name   Map of all output tensors
 * @param boxes_quant       Quantized box tensor reshaped to {N, 4}
 * @param classes_quant     Quantized class tensor reshaped to {N, 1}
 * @param landmarks_quant   Quantized landmark tensor reshaped to {N, 10}
 * @param anchors           Full anchor array (all scales concatenated)
 * @param score_threshold   Confidence threshold for face detections
 * @param class_layer_name  Name of the class tensor (for quantization params)
 * @param box_layer_name    Name of the box tensor (for quantization params)
 * @param landmark_layer_name Name of the landmark tensor (for quantization params)
 * @param anchor_offset     Offset into the global anchors array for this branch
 *
 * @return Tuple of (decoded_boxes, scores, decoded_landmarks) for detections above threshold
 */
static std::tuple<xt::xarray<float>, xt::xarray<float>, xt::xarray<float>> decode_branch(
    std::map<std::string, HailoTensorPtr> &tensors_by_name, const xt::xarray<uint8_t> &boxes_quant,
    const xt::xarray<uint8_t> &classes_quant, const xt::xarray<uint8_t> &landmarks_quant,
    const xt::xarray<float> &anchors, float score_threshold, const std::string &class_layer_name,
    const std::string &box_layer_name, const std::string &landmark_layer_name, int anchor_offset)
{
    auto class_tensor = tensors_by_name[class_layer_name];
    float class_scale = class_tensor->qp_scale();
    float class_zp = class_tensor->qp_zp();

    // Collect indices of anchors that pass the score threshold.
    // Iterate the flat classes tensor — each element is one anchor's face score.
    int num_anchors = static_cast<int>(classes_quant.shape(0));
    std::vector<size_t> passing_indices;
    for (int idx = 0; idx < num_anchors; ++idx)
    {
        float score = (static_cast<float>(classes_quant(idx, 0)) - class_zp) * class_scale;
        if (score > score_threshold)
        {
            passing_indices.push_back(static_cast<size_t>(idx));
        }
    }

    if (passing_indices.empty())
    {
        return std::make_tuple(xt::empty<float>({0}), xt::empty<float>({0}), xt::empty<float>({0}));
    }

    int count = static_cast<int>(passing_indices.size());
    auto box_tensor = tensors_by_name[box_layer_name];
    auto landmark_tensor = tensors_by_name[landmark_layer_name];

    // Build filtered + dequantized arrays manually to avoid xtensor view pitfalls
    xt::xarray<float> filtered_boxes = xt::zeros<float>({count, COORDS_PER_BOX});
    xt::xarray<float> filtered_scores = xt::zeros<float>({count});
    xt::xarray<float> filtered_landmarks = xt::zeros<float>({count, LANDMARK_VALUES_PER_ANCHOR});
    xt::xarray<float> filtered_anchors = xt::zeros<float>({count, 4});

    float box_scale = box_tensor->qp_scale();
    float box_zp = box_tensor->qp_zp();
    float lm_scale = landmark_tensor->qp_scale();
    float lm_zp = landmark_tensor->qp_zp();

    for (int out = 0; out < count; ++out)
    {
        size_t src = passing_indices[out];

        // Score
        filtered_scores(out) = (static_cast<float>(classes_quant(src, 0)) - class_zp) * class_scale;

        // Boxes
        for (int c = 0; c < COORDS_PER_BOX; ++c)
        {
            filtered_boxes(out, c) = (static_cast<float>(boxes_quant(src, c)) - box_zp) * box_scale;
        }

        // Landmarks
        for (int c = 0; c < LANDMARK_VALUES_PER_ANCHOR; ++c)
        {
            filtered_landmarks(out, c) = (static_cast<float>(landmarks_quant(src, c)) - lm_zp) * lm_scale;
        }

        // Anchors
        size_t anchor_idx = src + static_cast<size_t>(anchor_offset);
        for (int c = 0; c < 4; ++c)
        {
            filtered_anchors(out, c) = anchors(anchor_idx, c);
        }
    }

    xt::xarray<float> decoded_boxes = decode_boxes(filtered_boxes, filtered_anchors);
    xt::xarray<float> decoded_landmarks = decode_landmarks(filtered_landmarks, filtered_anchors);

    return std::make_tuple(std::move(decoded_boxes), std::move(filtered_scores), std::move(decoded_landmarks));
}

// ============================================================================
// Detection Encoding
// ============================================================================

/**
 * @brief Package decoded boxes, scores, and landmarks into HailoDetection objects.
 *
 * Each detection gets a "face" label and has 5 landmarks attached via
 * hailo_common::add_landmarks_to_detection.
 */
static void encode_detections(std::vector<HailoDetection> &detections, std::vector<xt::xarray<float>> &decoded_boxes,
                              std::vector<xt::xarray<float>> &scores, std::vector<xt::xarray<float>> &decoded_landmarks,
                              int num_branches)
{
    static const std::string FACE_LABEL = "face";

    for (int branch = 0; branch < num_branches; ++branch)
    {
        for (size_t detection_idx = 0; detection_idx < scores[branch].size(); ++detection_idx)
        {
            float confidence = scores[branch](detection_idx);
            float xmin = decoded_boxes[branch](detection_idx, 0);
            float ymin = decoded_boxes[branch](detection_idx, 1);
            float width = decoded_boxes[branch](detection_idx, 2) - xmin;
            float height = decoded_boxes[branch](detection_idx, 3) - ymin;

            HailoBBox bbox(xmin, ymin, width, height);
            HailoDetection detection(bbox, FACE_LABEL, confidence);

            // Extract the 10 landmark values for this detection and reshape to (5, 2)
            xt::xarray<float> keypoints_raw = xt::row(decoded_landmarks[branch], detection_idx);
            auto face_keypoints = xt::reshape_view(keypoints_raw, {LANDMARKS_PER_FACE, COORDS_PER_LANDMARK});
            hailo_common::add_landmarks_to_detection(detection, "scrfd", face_keypoints);

            detections.push_back(std::move(detection));
        }
    }
}

// ============================================================================
// Main Postprocess Pipeline
// ============================================================================

/**
 * @brief Full SCRFD face detection postprocess pipeline.
 *
 * 1. Gather and reshape quantized tensors from all output branches
 * 2. Filter by score threshold and dequantize (per branch)
 * 3. Decode boxes and landmarks using precomputed anchors
 * 4. Encode into HailoDetection objects with landmarks
 * 5. Apply NMS to remove duplicate detections
 */
static std::vector<HailoDetection> scrfd_postprocess(std::map<std::string, HailoTensorPtr> &tensors_by_name,
                                                     const ScrfdParams &params)
{
    std::vector<HailoDetection> detections;

    const int num_branches = params.m_num_branches;

    // Gather and reshape tensors from each branch
    std::vector<xt::xarray<uint8_t>> box_layers_quant;
    std::vector<xt::xarray<uint8_t>> class_layers_quant;
    std::vector<xt::xarray<uint8_t>> landmark_layers_quant;

    box_layers_quant.reserve(num_branches);
    class_layers_quant.reserve(num_branches);
    landmark_layers_quant.reserve(num_branches);

    for (int branch = 0; branch < num_branches; ++branch)
    {
        // Boxes: reshape from {H, W, anchors*4} to {H*W*anchors, 4}
        xt::xarray<uint8_t> raw_boxes = common::get_xtensor(tensors_by_name[params.m_box_layer_names[branch]]);
        int num_boxes = static_cast<int>(raw_boxes.shape(0)) * static_cast<int>(raw_boxes.shape(1)) *
                        (static_cast<int>(raw_boxes.shape(2)) / COORDS_PER_BOX);
        box_layers_quant.emplace_back(xt::reshape_view(raw_boxes, {num_boxes, COORDS_PER_BOX}));

        // Classes: reshape from {H, W, anchors*1} to {H*W*anchors, 1}
        xt::xarray<uint8_t> raw_classes = common::get_xtensor(tensors_by_name[params.m_class_layer_names[branch]]);
        int num_anchors = static_cast<int>(raw_classes.shape(0)) * static_cast<int>(raw_classes.shape(1)) *
                          static_cast<int>(raw_classes.shape(2));
        class_layers_quant.emplace_back(xt::reshape_view(raw_classes, {num_anchors, 1}));

        // Landmarks: reshape from {H, W, anchors*10} to {H*W*anchors, 10}
        xt::xarray<uint8_t> raw_landmarks = common::get_xtensor(tensors_by_name[params.m_landmark_layer_names[branch]]);
        int num_landmark_anchors = static_cast<int>(raw_landmarks.shape(0)) * static_cast<int>(raw_landmarks.shape(1)) *
                                   (static_cast<int>(raw_landmarks.shape(2)) / LANDMARK_VALUES_PER_ANCHOR);
        landmark_layers_quant.emplace_back(
            xt::reshape_view(raw_landmarks, {num_landmark_anchors, LANDMARK_VALUES_PER_ANCHOR}));
    }

    // Decode each branch
    std::vector<xt::xarray<float>> all_decoded_boxes(num_branches);
    std::vector<xt::xarray<float>> all_scores(num_branches);
    std::vector<xt::xarray<float>> all_decoded_landmarks(num_branches);

    int anchor_offset = 0;
    for (int branch = 0; branch < num_branches; ++branch)
    {
        auto [branch_boxes, branch_scores, branch_landmarks] = decode_branch(
            tensors_by_name, box_layers_quant[branch], class_layers_quant[branch], landmark_layers_quant[branch],
            params.m_anchors, params.m_score_threshold, params.m_class_layer_names[branch],
            params.m_box_layer_names[branch], params.m_landmark_layer_names[branch], anchor_offset);
        all_decoded_boxes[branch] = std::move(branch_boxes);
        all_scores[branch] = std::move(branch_scores);
        all_decoded_landmarks[branch] = std::move(branch_landmarks);
        anchor_offset += static_cast<int>(class_layers_quant[branch].shape(0));
    }

    // Encode into HailoDetection objects
    encode_detections(detections, all_decoded_boxes, all_scores, all_decoded_landmarks, num_branches);

    // Apply NMS
    common::nms(detections, params.m_iou_threshold);

    return detections;
}

// ============================================================================
// Core Entry Point
// ============================================================================

static void scrfd(HailoROIPtr roi, void *params_void_ptr)
{
    if (!roi->has_tensors())
        return;

    auto *params = reinterpret_cast<ScrfdParams *>(params_void_ptr);
    std::map<std::string, HailoTensorPtr> tensors_by_name = roi->get_tensors_by_name();

    std::vector<HailoDetection> detections = scrfd_postprocess(tensors_by_name, *params);

    hailo_common::add_detections(roi, detections);
}

// ============================================================================
// JSON Config Parsing
// ============================================================================

/**
 * @brief Parse SCRFD config from a JSON file using RapidJSON.
 *
 * Expected schema:
 * {
 *   "image_width": int,
 *   "image_height": int,
 *   "anchor_variance": [float, ...],
 *   "anchor_steps": [int, ...],
 *   "anchor_min_size": [[int, ...], ...],
 *   "score_threshold": float,
 *   "iou_threshold": float
 * }
 */
static ScrfdParams *parse_config(const std::string &config_path, const std::vector<std::string> &box_names,
                                 const std::vector<std::string> &class_names,
                                 const std::vector<std::string> &landmark_names)
{
    std::string config_content = common::read_file(config_path);
    rapidjson::Document doc;
    doc.Parse(config_content.c_str());

    if (doc.HasParseError())
    {
        throw std::runtime_error("SCRFD: JSON parse error in " + config_path + ": " +
                                 rapidjson::GetParseError_En(doc.GetParseError()));
    }

    int image_width = doc["image_width"].GetInt();
    int image_height = doc["image_height"].GetInt();
    float score_threshold = doc["score_threshold"].GetFloat();
    float iou_threshold = doc["iou_threshold"].GetFloat();

    // Parse anchor_variance
    std::vector<float> anchor_variance_vec;
    for (auto &val : doc["anchor_variance"].GetArray())
    {
        anchor_variance_vec.push_back(val.GetFloat());
    }

    // Parse anchor_steps
    std::vector<int> anchor_steps_vec;
    for (auto &val : doc["anchor_steps"].GetArray())
    {
        anchor_steps_vec.push_back(val.GetInt());
    }

    // Parse anchor_min_size
    std::vector<std::vector<int>> anchor_min_size;
    for (auto &group : doc["anchor_min_size"].GetArray())
    {
        std::vector<int> sizes;
        for (auto &val : group.GetArray())
        {
            sizes.push_back(val.GetInt());
        }
        anchor_min_size.push_back(std::move(sizes));
    }

    xt::xarray<float> anchor_variance = xt::adapt(anchor_variance_vec);
    xt::xarray<float> anchors = generate_anchors(anchor_min_size, anchor_steps_vec, image_width, image_height);
    int num_branches = static_cast<int>(anchor_min_size.size());

    auto *params = new ScrfdParams();
    params->m_anchors = std::move(anchors);
    params->m_anchor_variance = std::move(anchor_variance);
    params->m_anchor_min_size = std::move(anchor_min_size);
    params->m_score_threshold = score_threshold;
    params->m_iou_threshold = iou_threshold;
    params->m_num_branches = num_branches;
    params->m_box_layer_names = box_names;
    params->m_class_layer_names = class_names;
    params->m_landmark_layer_names = landmark_names;

    return params;
}

// ============================================================================
// Exported C Functions
// ============================================================================

ScrfdParams *init(const std::string config_path, const std::string /*function_name*/)
{
    if (!fs::exists(config_path))
    {
        HAILO_POSTPROCESS_LOG_ERROR("[SCRFD] Required config file not found: {}", config_path);
        throw std::runtime_error("SCRFD: required config file not found: " + config_path);
    }

    return parse_config(config_path, BOXES_10G, CLASSES_10G, LANDMARKS_10G);
}

void free_resources(void *params_void_ptr)
{
    auto *params = reinterpret_cast<ScrfdParams *>(params_void_ptr);
    delete params;
}

void filter(HailoROIPtr roi, void *params_void_ptr)
{
    scrfd(roi, params_void_ptr);
}

void scrfd_10g(HailoROIPtr roi, void *params_void_ptr)
{
    scrfd(roi, params_void_ptr);
}
