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

namespace hailo_analytics::analytics::dynamic_privacy_mask
{

namespace cropping_stages = hailo_analytics::pipeline::cropping;
using hailo_analytics::analytics::aggregator_config_t;

// Dynamic privacy mask pipeline parameters
inline constexpr std::string_view DYNAMIC_PRIVACY_MASK_PIPELINE = "dynamic_privacy_mask_pipeline";
inline constexpr std::string_view BBOX_CROP_SEGMENTATION_PIPELINE = "bbox_crop_segmentation_pipeline";
inline constexpr std::string_view SEGMENTATION_SUBPIPELINE = "segmentation_subpipeline";

// Semantic segmentation AI Stage parameters
inline constexpr std::string_view SEGMENTATION_STAGE = "semantic_segmentation";
inline constexpr std::string_view SEGMENTATION_BASE_HEF =
    "/home/root/apps/dynamic_privacy_mask/resources/linknet_mbv1_ss_dpm_128.hef";
inline constexpr std::string_view SEGMENTATION_GROUP_ID = "device0";

// Semantic segmentation Postprocess parameters
inline constexpr std::string_view SEGMENTATION_POST_STAGE = "segmentation_post";
inline constexpr std::string_view SEGMENTATION_POST_SO = "/usr/lib/hailo-post-processes/liblinknet_post.so";
inline constexpr std::string_view SEGMENTATION_POST_FUNCTION = "linknet_post";
inline constexpr std::string_view SEGMENTATION_POST_CONF = "";

// BBox Crop Stage parameters
inline constexpr std::string_view BBOX_CROP_STAGE = "bbox_crops";
inline constexpr std::string_view SEGMENTATION_AGGREGATOR_STAGE = "segmentation_aggregator";

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
    std::optional<bool> use_letterbox;
    std::optional<dsp_letterbox_alignment_t> letterbox_alignment;
    std::optional<dsp_color_t> letterbox_color;

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
 * @brief Configuration for semantic segmentation pipeline.
 *
 * Uses the common ai_postprocess_pair_config_t pattern for configuring
 * the AI inference stage and postprocess stage.
 */
struct segmentation_config_t : public ai_postprocess_pair_config_t
{
    // Inherits ai_config and post_config from ai_postprocess_pair_config_t
};

/**
 * @brief Combined configuration for bbox crop + semantic segmentation pipeline.
 *
 * This structure combines bbox crop, segmentation, and aggregator configurations
 * for a complete bbox crop + segmentation + aggregation pipeline.
 */
struct bbox_crop_segmentation_config_t
{
    bbox_crop_config_t bbox_crop_config;
    segmentation_config_t segmentation_config;
    aggregator_config_t aggregator_config;

    /**
     * @brief Merge configuration from another bbox_crop_segmentation_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const bbox_crop_segmentation_config_t &other);

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
 * @brief Get default configuration for semantic segmentation pipeline only.
 *
 * @return segmentation_config_t with sensible defaults
 */
segmentation_config_t segmentation_base_config();

/**
 * @brief Get default configuration for bbox crop + segmentation pipeline.
 *
 * @return bbox_crop_segmentation_config_t with sensible defaults
 */
bbox_crop_segmentation_config_t base_config();

/**
 * @brief Generate a semantic segmentation pipeline with the given configuration.
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed segmentation pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_segmentation_pipeline(const std::string &pipeline_name = std::string(DYNAMIC_PRIVACY_MASK_PIPELINE),
                               std::optional<segmentation_config_t> configs = std::nullopt);

/**
 * @brief Generate a bbox crop + semantic segmentation pipeline with the given configuration.
 *
 * Creates a pipeline where:
 * - BBox crop stage crops detected objects from input
 * - Semantic segmentation sub-pipeline (generated using generate_segmentation_pipeline) processes each crop
 * - Aggregator combines the segmentation results back together
 * - Main output of bbox crop connects to main input of aggregator
 * - BBox crops feed into segmentation sub-pipeline
 * - Segmentation sub-pipeline output feeds into aggregator sub input
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed dynamic privacy mask pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_dynamic_privacy_mask_pipeline(const std::string &pipeline_name = std::string(BBOX_CROP_SEGMENTATION_PIPELINE),
                                       std::optional<bbox_crop_segmentation_config_t> configs = std::nullopt);

} // namespace hailo_analytics::analytics::dynamic_privacy_mask
