/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"
namespace common
{

float iou_calc(const HailoBBox &box_1, const HailoBBox &box_2);

/**
 * @brief Perform IOU based NMS on a vector of HailoDetection objects
 *
 * @param objects  -  std::vector<HailoDetection>
 *        The detections to perform NMS on.
 *
 * @param iou_thr  -  float
 *        Threshold for IOU filtration
 *
 * @param should_nms_cross_classes  -  bool
 *        If true, then apply NMS regardless of class differences. Default false.
 */
void nms(std::vector<HailoDetection> &objects, const float iou_thr, bool should_nms_cross_classes = false);

} // namespace common
