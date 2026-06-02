/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_tensors.hpp"

#define MEDIAPIPE_LANDMARK_COUNT 468

float get_face_presence_score(HailoTensorPtr tensor);

// Main processing logic to extract and add landmarks to ROI
void mediapipe_landmark(HailoROIPtr roi);

// Hailo plugin interface function
extern "C" void facial_landmarks_nv12(HailoROIPtr roi);

// Fallback filter function used by post-process plugin loader
void filter(HailoROIPtr roi);
