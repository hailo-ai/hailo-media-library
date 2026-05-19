/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "mediapipe_landmarks_post.hpp"
#include "common/tensors.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

#include "xtensor/xarray.hpp"
#include "xtensor/xview.hpp"
#include "xtensor/xstrided_view.hpp"
#include "xtensor/xio.hpp"

#include <iostream>
#include <unordered_set>

#define MEDIAPIPE_LANDMARK_COUNT 468
#define MEDIAPIPE_INPUT_WIDTH 192
#define MEDIAPIPE_INPUT_HEIGHT 192
#define MEDIAPIPE_PRESENCE_SCORE 0.5f
//******************************************************************
// MEDIAPIPE FACIAL LANDMARK POST-PROCESSING
//******************************************************************

/*
The MediaPipe Face Landmarker model is a lightweight neural network that predicts
468 3D facial landmarks from an input face image. Each landmark consists of an (x, y, z)
coordinate, representing normalized positions relative to the cropped face region.

This post-processing module interprets the raw tensor output from the model,
dequantizes and normalizes the values, and groups selected indices
(e.g., eyes, nose, mouth) for downstream rendering or analysis.
*/

// Name of the output layer containing MediaPipe facial landmarks
const char *output_layer_name = "face_landmarks_lite/conv22";

// Subsets of landmark indices for specific facial regions
static const std::unordered_set<int> EYE_INDICES = {33,  133, 159, 145, 153, 154, 155, 246,
                                                    362, 263, 387, 373, 380, 381, 382, 466};
static const std::unordered_set<int> NOSE_INDICES = {168, 197, 5, 98, 2, 326};
static const std::unordered_set<int> MOUTH_INDICES = {78, 191, 80, 95, 88, 178, 87, 14, 317, 402, 318, 324};

// Dequantize tensor and convert to normalized [x, y, z] landmark array
xt::xarray<float> get_normalized_landmarks(HailoTensorPtr tensor)
{
    // Grab raw INT8 output and de-quantise
    auto raw = ::common::get_xtensor(tensor); // shape = {1,1,1404}
    float qp_scale = tensor->qp_scale();
    float qp_zp = tensor->qp_zp();
    xt::xarray<uint8_t> flat = xt::eval(xt::flatten(raw)); // shape = {1404}
    xt::xarray<float> dequant = common::dequantize(flat, qp_scale, qp_zp);

    // Re-shape into (468,3) view – no copy yet
    auto landmarks_view = xt::reshape_view(dequant, {MEDIAPIPE_LANDMARK_COUNT, 3});

    // Normalise x / y in a single broadcast operation
    static const xt::xarray<float> scale = {1.f / MEDIAPIPE_INPUT_WIDTH, 1.f / MEDIAPIPE_INPUT_HEIGHT, 1.f};
    // Materialise the result so the caller gets a concrete xarray
    return xt::eval(landmarks_view * scale);
}

float get_face_presence_score(HailoTensorPtr tensor)
{
    auto raw = ::common::get_xtensor(tensor); // shape: (1, 1, 1)
    float scale = tensor->qp_scale();
    int zp = tensor->qp_zp();
    return (raw(0, 0, 0) - zp) * scale;
}

// Parse and add landmark annotations to the ROI
void mediapipe_landmark(HailoROIPtr roi)
{
    if (!roi->has_tensors())
        return;

    auto presence_tensor = roi->get_tensor("face_landmarks_lite/conv25");
    float presence_score = get_face_presence_score(presence_tensor);
    if (presence_score < MEDIAPIPE_PRESENCE_SCORE)
        return;

    auto tensor = roi->get_tensor(output_layer_name);
    xt::xarray<float> landmarks = get_normalized_landmarks(tensor);

    // Add all landmarks as "landmarks" object
    std::vector<HailoPoint> all_points;
    all_points.reserve(MEDIAPIPE_LANDMARK_COUNT);
    for (size_t i = 0; i < MEDIAPIPE_LANDMARK_COUNT; ++i)
    {
        all_points.emplace_back(landmarks(i, 0), landmarks(i, 1));
    }
    roi->add_object(std::make_shared<HailoLandmarks>("face", all_points));
}

// Main entry point used by Hailo runtime to call post-process
void facial_landmarks_nv12(HailoROIPtr roi)
{
    mediapipe_landmark(roi);
}

void filter(HailoROIPtr roi)
{
    facial_landmarks_nv12(roi);
}
