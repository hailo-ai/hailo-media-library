#pragma once

#include <vector>
#include <unordered_set>

// Postprocess Tools includes (needed for HailoBBox type used in TILES vector)
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

#define VISION_SINK "sink0"           // The streamid from frontend to 4K stream that shows vision results
#define SECONDARY_VISION_SINK "sink1" // The small streamid from frontend that would be used for overlay
#define AI_SINK "sink2"               // The streamid from frontend to AI (FHD)
#define CALLBACK_STAGE "callback_stage"

// Output Params
#define HOST_IP "10.0.0.2"
#define TRACKER_STAGE "tracker"
#define OVERLAY_STAGE_SINK0 "overlay_sink0"
#define OVERLAY_STAGE_SINK1 "overlay_sink1"
#define DEMUX_STAGE "demuxer"

// Tiling Params
#define MUXER_STAGE "muxer"

#define TILING_STAGE "tiling"
#define TILING_INPUT_WIDTH 1920
#define TILING_INPUT_HEIGHT 1080
#define TILING_OUTPUT_WIDTH 640
#define TILING_OUTPUT_HEIGHT 384
std::vector<HailoBBox> TILES = {
    {0.0, 0.0, 0.6, 0.6}, {0.4, 0, 0.6, 0.6}, {0, 0.4, 0.6, 0.6}, {0.4, 0.4, 0.6, 0.6}, {0.0, 0.0, 1.0, 1.0}};
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/hailo_yolov8n_384_640.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "hailo_yolov8n"
#define YOLO_POST_CONF "/home/root/apps/webserver/resources/configs/yolov5_personface.json"
// Stage 1 Aggregator Params
#define DETECTION_AGGREGATOR "detection_aggregator"
#define STAGE_1_AGGREGATOR "stage_1_aggregator"

/*
    Stage 2 Params (Face Landmarks)
*/
// Tee Params
#define TEE_STAGE "vision_tee"
// Bbox crop Parms
#define BBOX_CROP_STAGE "bbox_crops"
#define BBOX_CROP_LABEL "face"
#define BBOX_CROP_OUTPUT_WIDTH 192
#define BBOX_CROP_OUTPUT_HEIGHT 192
// Landmarks AI Params
#define LANDMARKS_HEF_FILE "/home/root/apps/ai_example_app/resources/face_landmarks_lite.hef"
#define LANDMARKS_AI_STAGE "face_landmarks"
// Landmarks Postprocess Params
#define LANDMARKS_POST_STAGE "landmarks_post"
#define LANDMARKS_POST_SO "/usr/lib/hailo-post-processes/libmediapipe_post.so"
#define LANDMARKS_FUNC_NAME "facial_landmarks_nv12"
// Example: indices to draw for landmarks (used if not full_landmarks)
const std::unordered_set<size_t> LANDMARKS_INDICES_EXAMPLE = {33, 468, 133, 362, 473, 263, 5, 4, 1};
// Stage 2 Aggregator Params
#define LANDMARKS_AGGREGATOR "landmarks_aggregator"
#define STAGE_2_AGGREGATOR "stage_2_aggregator"
