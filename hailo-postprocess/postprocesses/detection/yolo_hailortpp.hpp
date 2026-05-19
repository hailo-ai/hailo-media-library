/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once
#include <sys/types.h>
#include <cstdint>
#include <map>
#include <string>

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

__BEGIN_DECLS

class YoloParamsNMS
{
  public:
    std::map<std::uint8_t, std::string> labels;
    float detection_threshold;
    uint max_boxes;
    bool filter_by_score = false;
    YoloParamsNMS(std::map<uint8_t, std::string> dataset = std::map<uint8_t, std::string>(),
                  float detection_threshold = 0.3f, uint max_boxes = 200)
        : labels(dataset), detection_threshold(detection_threshold), max_boxes(max_boxes)
    {
    }
};

YoloParamsNMS *init(const std::string config_path, const std::string function_name);
void free_resources(void *params_void_ptr);
void filter(HailoROIPtr roi, void *params_void_ptr);
void filter_letterbox(HailoROIPtr roi, void *params_void_ptr);
void yolov5(HailoROIPtr roi);
void yolov5s_nv12(HailoROIPtr roi);
void yolov8s(HailoROIPtr roi);
void yolov8m(HailoROIPtr roi);
void yolox(HailoROIPtr roi);
void yolov8n_personface(HailoROIPtr roi, YoloParamsNMS *params);
void hailo_yolov8n(HailoROIPtr roi, YoloParamsNMS *params);
void hailo_yolov8s(HailoROIPtr roi, YoloParamsNMS *params);
void hailo_yolov8m(HailoROIPtr roi, YoloParamsNMS *params);
void yolov5s_personface(HailoROIPtr roi, YoloParamsNMS *params);
void yolov5_no_persons(HailoROIPtr roi);
void yolov5m_vehicles(HailoROIPtr roi);
void yolov5m_vehicles_nv12(HailoROIPtr roi);
void yolov5_seg(HailoROIPtr roi);
__END_DECLS
