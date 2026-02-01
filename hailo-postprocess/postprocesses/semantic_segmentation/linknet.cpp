/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "linknet.hpp"
#include "common/tensors.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

#include <vector>
#include <memory>

void linknet_post(HailoROIPtr roi)
{
    // Debug: verify tensors exist on ROI
    if (!roi->has_tensors())
    {
        return;
    }

    auto tensors = roi->get_tensors();

    // Process each output tensor as a separate class mask
    // The HEF has multiple outputs (slice2, slice3, etc.), each representing a different class
    for (auto &tensor : tensors)
    {
        // Each tensor should be HxWx1 format for semantic segmentation
        if (tensor->features() == 1)
        {
            // Create a class mask from the tensor data
            // The mask size is dynamic based on the tensor dimensions
            auto mask = std::make_shared<HailoClassMask>(tensor->data(), tensor->width(), tensor->height(),
                                                         0.5f // transparency value
            );
            roi->add_object(mask);
        }
    }
}
