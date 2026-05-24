/**
 * Copyright (c) 2026-2027 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include <string>
#include <vector>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "xtensor/xarray.hpp"

/**
 * @brief Parameters for SCRFD face detection + landmarks postprocessing.
 *
 * Stores precomputed anchors, quantization-aware thresholds, and per-model
 * output layer names so that all mutable state lives here rather than in globals.
 */
class ScrfdParams
{
  public:
    xt::xarray<float> m_anchors;
    xt::xarray<float> m_anchor_variance;
    std::vector<std::vector<int>> m_anchor_min_size;
    float m_score_threshold;
    float m_iou_threshold;
    int m_num_branches;

    // Per-model output layer names (populated once during init)
    std::vector<std::string> m_box_layer_names;
    std::vector<std::string> m_class_layer_names;
    std::vector<std::string> m_landmark_layer_names;
};

__BEGIN_DECLS
ScrfdParams *init(const std::string config_path, const std::string function_name);
void free_resources(void *params_void_ptr);
void filter(HailoROIPtr roi, void *params_void_ptr);
void scrfd_10g(HailoROIPtr roi, void *params_void_ptr);
__END_DECLS
