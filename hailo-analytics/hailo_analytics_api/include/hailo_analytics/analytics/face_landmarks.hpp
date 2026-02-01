#pragma once

#include <string_view>
#include <optional>
#include <string>
#include <cstddef>
#include "tl/expected.hpp"

#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"

namespace hailo_analytics::analytics::face_landmarks
{

namespace cropping_stages = hailo_analytics::pipeline::cropping;
using hailo_analytics::analytics::aggregator_config_t;

// Face landmarks pipeline parameters
inline constexpr std::string_view FACE_LANDMARKS_PIPELINE = "face_landmarks_pipeline";
inline constexpr std::string_view BBOX_CROP_LANDMARKS_PIPELINE = "bbox_crop_landmarks_pipeline";
inline constexpr std::string_view LANDMARKS_SUBPIPELINE = "landmarks_subpipeline";

// Face landmarks AI Stage parameters
inline constexpr std::string_view LANDMARKS_STAGE = "face_landmarks";
inline constexpr std::string_view LANDMARKS_BASE_HEF =
    "/home/root/apps/ai_example_app/resources/face_landmarks_lite.hef";
inline constexpr std::string_view LANDMARKS_GROUP_ID = "device0";

// Face landmarks Postprocess parameters
inline constexpr std::string_view LANDMARKS_POST_STAGE = "landmarks_post";
inline constexpr std::string_view LANDMARKS_POST_SO = "/usr/lib/hailo-post-processes/libmediapipe_post.so";
inline constexpr std::string_view LANDMARKS_POST_FUNCTION = "facial_landmarks_nv12";
inline constexpr std::string_view LANDMARKS_POST_CONF = "";

// BBox Crop Stage parameters
inline constexpr std::string_view BBOX_CROP_STAGE = "bbox_crops";
inline constexpr std::string_view LANDMARKS_AGGREGATOR_STAGE = "landmarks_aggregator";

/**
 * @brief Configuration structure for BBox crop stages.
 *
 * Contains all configurable parameters for BBoxCropStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct bbox_crop_config_t
{
    std::optional<std::string> stage_name;
    std::optional<int> output_pool_size;
    std::optional<int> input_width;
    std::optional<int> input_height;
    std::optional<int> output_width;
    std::optional<int> output_height;
    std::optional<std::string> main_sub_name;
    std::optional<std::string> sub_sub_name;
    std::optional<std::vector<std::string>> labels;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;
    std::optional<hailo_analytics::pipeline::StagePoolMode> pool_mode;
    std::optional<size_t> crop_every_x_frames;

    /**
     * @brief Merge configuration from another bbox_crop_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const bbox_crop_config_t &other);

    /**
     * @brief Apply this configuration to a BBoxCropStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::BBoxCropStageBuild::Builder &b) const;
};

/**
 * @brief Configuration for face landmarks pipeline.
 *
 * Uses the common ai_postprocess_pair_config_t pattern for configuring
 * the AI inference stage and postprocess stage.
 */
struct face_landmarks_config_t : public ai_postprocess_pair_config_t
{
    // Inherits ai_config and post_config from ai_postprocess_pair_config_t
};

/**
 * @brief Combined configuration for bbox crop + face landmarks pipeline.
 *
 * This structure combines bbox crop, face landmarks, and aggregator configurations
 * for a complete bbox crop + landmarks + aggregation pipeline.
 */
struct bbox_crop_landmarks_config_t
{
    bbox_crop_config_t bbox_crop_config;
    face_landmarks_config_t landmarks_config;
    aggregator_config_t aggregator_config;

    /**
     * @brief Merge configuration from another bbox_crop_landmarks_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const bbox_crop_landmarks_config_t &other);

    /**
     * @brief Apply bbox crop configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::BBoxCropStageBuild::Builder &b) const;

    /**
     * @brief Apply aggregator configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const;
};

/**
 * @brief Get default configuration for face landmarks pipeline only.
 *
 * @return face_landmarks_config_t with sensible defaults
 */
face_landmarks_config_t face_landmarks_base_config();

/**
 * @brief Get default configuration for bbox crop + landmarks pipeline.
 *
 * @return bbox_crop_landmarks_config_t with sensible defaults
 */
bbox_crop_landmarks_config_t base_config();

/**
 * @brief Generate a face landmarks pipeline with the given configuration.
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed face landmarks pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_face_landmarks_pipeline(const std::string &pipeline_name = std::string(FACE_LANDMARKS_PIPELINE),
                                 std::optional<face_landmarks_config_t> configs = std::nullopt);

/**
 * @brief Generate a bbox crop + face landmarks pipeline with the given configuration.
 *
 * Creates a pipeline where:
 * - BBox crop stage crops detected faces from input
 * - Face landmarks sub-pipeline (generated using generate_face_landmarks_pipeline) processes each crop
 * - Aggregator combines the landmark results back together
 * - Main output of bbox crop connects to main input of aggregator
 * - BBox crops feed into landmarks sub-pipeline
 * - Landmarks sub-pipeline output feeds into aggregator sub input
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed bbox crop landmarks pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_bbox_landmarks_pipeline(const std::string &pipeline_name = std::string(BBOX_CROP_LANDMARKS_PIPELINE),
                                 std::optional<bbox_crop_landmarks_config_t> configs = std::nullopt);

} // namespace hailo_analytics::analytics::face_landmarks
