/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

#include "clip_gen.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/tracking/hailo_tracker.hpp"

#define CLIP_RESNET_50X4_EXPECTED_NUM_OUTPUT (1)
#define CLIP_VIT_B_32_EXPECTED_NUM_OUTPUT (1)

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

    uint8_t *data_ptr = tensors[0]->data();
    size_t data_size = tensors[0]->size();
    std::vector<float> dequantized_data(data_size);
    // Dequantize the tensor data
    for (size_t i = 0; i < data_size; ++i)
    {
        dequantized_data[i] = tensors[0]->fix_scale(data_ptr[i]);
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
    std::cout << function_name << " total tensor: " << tensors.size() << std::endl;
    for (auto tensor : tensors)
    {
        std::cout << "Tensor name: " << tensor->name() << ", width: " << tensor->width() <<
                     ", height: " << tensor->height() << ", features: " << tensor->features() <<
                     ", size: " << tensor->size() << std::endl;

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
        std::cout << "WARNING: clip_resnet_50x4 total tensor: " << tensors.size() << ", but expecting "
                  << CLIP_RESNET_50X4_EXPECTED_NUM_OUTPUT << std::endl;
        return;
    }

    handle_clip_post_process(roi, 640, std::string(__func__));
}

void clip_vit_b_32(HailoROIPtr roi)
{
    auto tensors = roi->get_tensors();
    if (tensors.size() > CLIP_VIT_B_32_EXPECTED_NUM_OUTPUT)
    {
        std::cout << "WARNING: clip_vit_b_32 total tensor: " << tensors.size() << ", but expecting "
                  << CLIP_VIT_B_32_EXPECTED_NUM_OUTPUT << std::endl;
        return;
    }

    handle_clip_post_process(roi, 512, std::string(__func__));
}
