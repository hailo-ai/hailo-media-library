#pragma once

#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

namespace app
{

struct EmbeddingInfo
{
    struct EmbeddingData
    {
        std::string prompt;
        std::vector<float> embedding;
    };

    std::string network_id;
    EmbeddingData positive_embedding;
    std::vector<EmbeddingData> negative_embeddings; // For multi-prompt support
    float score_threshold;
    int max_query;
    int remove_duplicate_within_sec;
    bool text_decode_on_device = true;
};

// Image data structure for thumbnail gallery
struct ImageData
{
    std::string jpeg_data;   // Base64 encoded JPEG data
    std::string description; // Text description
    int64_t timestamp;       // Epoch time in milliseconds
    float score;             // score of this query result
};

// Error types
enum class ImageError
{
    FILE_NOT_FOUND,
    INVALID_FORMAT,
    ENCODING_ERROR,
    NETWORK_ERROR,
    JSON_PARSE_ERROR
};

//==============================================================================
// File System & Storage Configuration
//==============================================================================

/**
 * @brief Defines paths to configuration files, AI models, and libraries.
 */
namespace paths
{
inline const std::string medialib_config = "/etc/imaging/cfg/medialib_configs/clip_example_medialib_config.json";
inline const std::string medialib_config_play_from_file =
    "/etc/imaging/cfg/medialib_configs/clip_example_medialib_config_play_from_file.json";

inline const std::string clip_app_config = "/home/root/apps/clip/resources/configs/clip_app_config.yaml";

inline const std::string clip_storage_mount_point = "/var/volatile";

// YOLOv8 model paths and names
inline const std::string yolo_hef = "/home/root/apps/face_landmarks/resources/hailo_yolov8n_384_640.hef";
inline const std::string yolo_post_so = "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so";
inline const std::string yolo_func_name = "hailo_yolov8n";
inline const std::string yolo_config = "/home/root/apps/clip/resources/configs/yolov8n_personface.json";
} // namespace paths

/**
 * @brief Defines medialib profile.
 */
namespace medialib_profile
{
inline const std::string play_from_file = "PlayFromFile";

} // namespace medialib_profile

/**
 * @brief Defines constants for file storage, such as database names and file prefixes.
 */
namespace storage
{
inline const double save_to_memory_min_gb = 3.0;
inline const std::string clip_database_file = "clip_database.db";
inline const std::string video_segment_prefix = "video_segment";
inline const std::string thumbnail_prefix = "thumbnail_vga";
} // namespace storage

//==============================================================================
// Network & Stream Configuration
//==============================================================================

/**
 * @brief Network-related constants like IP addresses and ports.
 */
namespace net
{
inline const std::string host_ip = "10.0.0.2";
inline constexpr int udp_port_4k = 5000;
} // namespace net

/**
 * @brief Unique identifiers for different video/data streams in the pipeline.
 */
namespace stream_id
{
inline std::string highres = "HighRes";
inline std::string stream_vga = "VGA";
inline std::string stream_ai = "AI";
} // namespace stream_id

//==============================================================================
// AI / Detection Configuration
//==============================================================================

/**
 * @brief Defines the classes/labels for object detection.
 */
namespace classes
{
enum class detection_id : int
{
    person = 1,
    vehicle = 2,
    face = 3,
    license_plate = 4
};
inline const std::string clip_crop_target_label_person = "person";
inline const std::string clip_crop_target_label_vehicle = "vehicle";
inline const std::string clip_crop_target_label_face = "face";
inline const std::string clip_crop_target_label_license_plate = "license_plate";
inline const std::string clip_crop_target_label_scene = "scene";

} // namespace classes

/**
 * @brief A simple structure to hold width and height dimensions.
 */
struct size
{
    int width;
    int height;
};

/**
 * @brief Tiling configuration for the AI inference input.
 */
namespace tiling
{
inline constexpr size input{1920, 1080};
inline constexpr size output{640, 384};

// Normalized tiles [x, y, w, h] for inference.
// Assumes HailoBBox is defined elsewhere (e.g., as a struct or type alias).
using tile = HailoBBox;
inline std::vector<tile> tiles = {
    tile{0.0, 0.0, 0.6, 0.6}, tile{0.4, 0.0, 0.6, 0.6}, tile{0.0, 0.4, 0.6, 0.6}, tile{0.4, 0.4, 0.6, 0.6},
    tile{0.0, 0.0, 1.0, 1.0} // Full frame
};
} // namespace tiling

//==============================================================================
// Pipeline Stage Identifiers
//==============================================================================

/**
 * @brief Unique string identifiers for each stage in the processing pipeline.
 */
namespace stage
{
// VGA stream stages
inline const std::string vga_tee = "vga_tee_stage";
inline const std::string vga_aggregator = "vga_agg_stage";
inline const std::string vga_overlay = "vga_overlay_stage";
inline const std::string thumbnail_storage = "thumb_storage_stage";
inline const std::string thumbnail_cache = "thumb_cache_stage";

// Main 4K stream stages
inline const std::string main_4k_tee = "main_4k_tee_stage";
inline const std::string main_4k_aggregator = "main_4k_agg_stage";
inline const std::string main_4k_overlay = "main_4k_overlay_stage";
inline const std::string main_mkv_storage = "main_mkv_storage_stage";
inline const std::string main_4k_udp = "main_4k_udp_stage";
inline const std::string main_4k_webrtc = "main_4k_webrtc_stage";

// Detection and tracking stages
inline const std::string detection_tiling = "detection_tiling_stage";
inline const std::string tiling_aggregator = "tiling_agg_stage";
inline const std::string detection_infer = "detection_infer_stage";
inline const std::string detection_post = "detection_post_stage";
inline const std::string detection_tee_out = "det_tee_out_stage";
inline const std::string tracker_light = "det_tracker_light_stage";

// Clip generation stages
inline const std::string tracker_traffic_ctrl = "tracker_traffic_control_stage";
inline const std::string clip_crop = "clip_crop_stage";
inline const std::string clip_image_preprocess_check = "clip_image_preprocess_check_stage";
inline const std::string clip_tee = "clip_tee_stage";
inline const std::string faiss_storage = "faiss_storage_stage";
inline const std::string full_frame_bbox_injector = "full_frame_bbox_injector_stage";
} // namespace stage

} // namespace app
