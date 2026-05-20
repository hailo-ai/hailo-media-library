/**
 * Copyright (c) 2026-2027 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include <string>
#include <vector>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

struct GalleryEntry
{
    std::string m_name;
    std::vector<std::vector<float>> m_embeddings;
};

class ArcfaceParams
{
  public:
    float m_similarity_threshold;
    std::string m_output_layer;
    std::string m_gallery_path;
    // Populated once during init(), read-only thereafter — no mutex required.
    std::vector<GalleryEntry> m_gallery;
};

__BEGIN_DECLS
ArcfaceParams *init(const std::string config_path, const std::string function_name);
void free_resources(void *params_void_ptr);
void filter(HailoROIPtr roi, void *params_void_ptr);
void arcface_nv12(HailoROIPtr roi, void *params_void_ptr);
void arcface_r50_nv12(HailoROIPtr roi, void *params_void_ptr);
void arcface_mobilenet_nv12(HailoROIPtr roi, void *params_void_ptr);
__END_DECLS
