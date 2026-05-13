/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

#include "clip_gen.hpp"
#include "hailo_postprocess_tools/logger/hailo_postprocess_logger.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/tracking/hailo_tracker.hpp"

#define CLIP_RESNET_50X4_EXPECTED_NUM_OUTPUT (1)
#define CLIP_VIT_B_32_EXPECTED_NUM_OUTPUT (1)
#define CLIP_VIT_L_14_LAION2B_EXPECTED_NUM_OUTPUT (1)

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

void handle_clip_post_process(HailoROIPtr roi, int vector_embedding_size, std::string function_name)
{
    auto tensors = roi->get_tensors();

    size_t data_size = tensors[0]->size();
    bool is_uint16 = tensors[0]->is_uint16();
    std::vector<float> dequantized_data(data_size);
    // Dequantize the tensor data (supports both UINT8 and UINT16 quantization)
    for (size_t i = 0; i < data_size; ++i)
    {
        // Tensor is 1D (1 x 1 x features), so row=0, col=0, channel=i
        dequantized_data[i] = tensors[0]->get_full_percision(0, 0, i, is_uint16);
    }

    l2_normalize_inplace(dequantized_data);

    // Add the quantize matrix so that we can save/process the inference result
    HailoMatrixPtr quantized_matrix = std::make_shared<HailoMatrix>(dequantized_data, tensors[0]->height(),
                                                                    tensors[0]->width(), tensors[0]->features());

    roi->add_object(quantized_matrix);

    // Add the function name (which we will be used to identify the CLIP network being used) and send it as user meta
    // This information is useful to store the index into the right database and faiss index so that we can
    // easily keep different CLIP network on our database
    HailoUserMetaPtr user_meta = std::make_shared<HailoUserMeta>(vector_embedding_size, function_name, 0.0f);
    roi->add_object(user_meta);

    /*
    HAILO_POSTPROCESS_LOG_DEBUG("{} total tensor: {}",
                                   function_name, tensors.size());

    for (auto tensor : tensors)
    {
        HAILO_POSTPROCESS_LOG_DEBUG("Tensor name: {}, width: {}, height: {}, features: {}, size: {}",
                                    tensor->name(), tensor->width(), tensor->height(), tensor->features(),
    tensor->size());
    }
    */
}

/**
 * @brief Process the ROI using the CLIP ResNet-50x4 model with NV12 format.
 *
 * @param roi A pointer to the region of interest (ROI).
 */
void clip_resnet_50x4(HailoROIPtr roi)
{
    auto tensors = roi->get_tensors();
    if (tensors.size() > CLIP_RESNET_50X4_EXPECTED_NUM_OUTPUT)
    {
        HAILO_POSTPROCESS_LOG_WARN("clip_resnet_50x4 total tensor: {}, but expecting {}", tensors.size(),
                                   CLIP_RESNET_50X4_EXPECTED_NUM_OUTPUT);
        return;
    }

    handle_clip_post_process(roi, 640, std::string(__func__));
}

void clip_vit_b_32(HailoROIPtr roi)
{
    auto tensors = roi->get_tensors();
    if (tensors.size() > CLIP_VIT_B_32_EXPECTED_NUM_OUTPUT)
    {
        HAILO_POSTPROCESS_LOG_WARN("clip_vit_b_32 total tensor: {}, but expecting {}", tensors.size(),
                                   CLIP_VIT_B_32_EXPECTED_NUM_OUTPUT);
        return;
    }

    handle_clip_post_process(roi, 512, std::string(__func__));
}

void clip_vit_l_14_laion2B(HailoROIPtr roi)
{
    auto tensors = roi->get_tensors();
    if (tensors.size() > CLIP_VIT_L_14_LAION2B_EXPECTED_NUM_OUTPUT)
    {
        HAILO_POSTPROCESS_LOG_WARN("clip_vit_l_14_laion2B total tensor: {}, but expecting {}", tensors.size(),
                                   CLIP_VIT_L_14_LAION2B_EXPECTED_NUM_OUTPUT);
        return;
    }

    // The post process for CLIP ViT L 14 Laion2B is the same as other CLIP post process, the only difference is the
    // embedding size and the function name which will be used to identify the CLIP network used for later database
    // indexing and searching
    handle_clip_post_process(roi, 768, std::string(__func__));
}
