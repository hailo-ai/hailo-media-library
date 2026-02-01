#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <cstddef>
#include "tl/expected.hpp"

#include "hailo_analytics/analytics/detection.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/pipeline/cropping/tiling_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

namespace hailo_analytics::analytics::tiling
{

namespace cropping_stages = hailo_analytics::pipeline::cropping;
using hailo_analytics::analytics::aggregator_config_t;

// Tiling pipeline parameters
inline constexpr std::string_view TILING_DETECTION_PIPELINE = "tiling_detection_pipeline";
inline constexpr std::string_view TILING_STAGE = "tiling_stage";
inline constexpr std::string_view TILING_AGGREGATOR_STAGE = "tiling_aggregator";
inline constexpr std::string_view DETECTION_SUBPIPELINE = "detection_subpipeline";

// Default tiles configuration
inline const std::vector<HailoBBox> DEFAULT_TILES = {
    {0.0, 0.0, 0.6, 0.6}, {0.4, 0, 0.6, 0.6}, {0, 0.4, 0.6, 0.6}, {0.4, 0.4, 0.6, 0.6}, {0.0, 0.0, 1.0, 1.0}};

/**
 * @brief Configuration structure for tiling stages.
 *
 * Contains all configurable parameters for TilingCropStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct tiling_config_t
{
    std::optional<std::string> stage_name;
    std::optional<int> output_pool_size;
    std::optional<int> input_width;
    std::optional<int> input_height;
    std::optional<int> output_width;
    std::optional<int> output_height;
    std::optional<std::string> main_sub_name;
    std::optional<std::string> sub_sub_name;
    std::optional<std::vector<HailoBBox>> bbox_tiles;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;
    std::optional<hailo_analytics::pipeline::StagePoolMode> pool_mode;
    std::optional<size_t> crop_every_x_frames;

    /**
     * @brief Merge configuration from another tiling_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const tiling_config_t &other);

    /**
     * @brief Apply this configuration to a TilingCropStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::TilingCropStageBuild::Builder &b) const;
};

/**
 * @brief Combined configuration for tiling detection pipeline.
 *
 * This structure combines tiling, detection, and aggregator configurations
 * for a complete tiling + detection + aggregation pipeline.
 */
struct tiling_detection_config_t
{
    tiling_config_t tiling_config;
    detection::detection_config_t detection_config;
    aggregator_config_t aggregator_config;

    /**
     * @brief Merge configuration from another tiling_detection_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const tiling_detection_config_t &other);

    /**
     * @brief Apply tiling configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::TilingCropStageBuild::Builder &b) const;

    /**
     * @brief Apply aggregator configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const;
};

/**
 * @brief Get default configuration for tiling detection pipeline.
 *
 * @return tiling_detection_config_t with sensible defaults
 */
tiling_detection_config_t base_config();

/**
 * @brief Generate a tiling detection pipeline with the given configuration.
 *
 * Creates a pipeline where:
 * - Tiling stage crops the input into tiles
 * - Detection sub-pipeline (generated using generate_detection_pipeline) processes each tile
 * - Aggregator combines the tiled detections back together
 * - Main output of tiling connects to main input of aggregator
 * - Tiling crops feed into detection sub-pipeline
 * - Detection sub-pipeline output feeds into aggregator sub input
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed tiling detection pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_tiling_detection_pipeline(const std::string &pipeline_name = std::string(TILING_DETECTION_PIPELINE),
                                   std::optional<tiling_detection_config_t> configs = std::nullopt);

} // namespace hailo_analytics::analytics::tiling
