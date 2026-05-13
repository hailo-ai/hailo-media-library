#pragma once

#include <string_view>
#include <optional>
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/core/pipeline_database.hpp"
#include "hailo_analytics/analytics/common_configs.hpp"
#include "hailo_analytics/pipeline/cropping/bbox_crop_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "tl/expected.hpp"

namespace hailo_analytics::analytics::license_plate_recognition
{

/**
 * @brief LPR-specific database entry storing OCR result and confidence scores.
 */
struct LprDBEntry : public hailo_analytics::pipeline::PipelineDBEntry
{
    std::string value;
    float confidence{0.0f};
    float detection_confidence{0.0f};
};

namespace cropping_stages = hailo_analytics::pipeline::cropping;
using hailo_analytics::analytics::aggregator_config_t;

// OCR pipeline parameters
inline constexpr std::string_view OCR_PIPELINE = "ocr_pipeline";

// OCR AI Stage parameters
inline constexpr std::string_view OCR_STAGE = "ocr_stage";
inline constexpr std::string_view OCR_BASE_HEF =
    "/home/root/apps/license_plate_recognition/resources/paddle_ocr_v5_mobile_recognition.hef";
inline constexpr std::string_view OCR_GROUP_ID = "device0";

// OCR Postprocess parameters
inline constexpr std::string_view OCR_POST_STAGE = "ocr_post";
inline constexpr std::string_view OCR_POST_SO = "/usr/lib/hailo-post-processes/libocr_post.so";

// BBox Crop OCR pipeline parameters
inline constexpr std::string_view BBOX_CROP_OCR_PIPELINE = "bbox_crop_ocr_pipeline";
inline constexpr std::string_view OCR_SUBPIPELINE = "ocr_subpipeline";
inline constexpr std::string_view BBOX_CROP_STAGE = "lp_bbox_crops";
inline constexpr std::string_view OCR_AGGREGATOR_STAGE = "ocr_aggregator";

// Quality gate stage parameters
inline constexpr std::string_view QUALITY_GATE_STAGE = "quality_gate";
inline constexpr std::string_view COMMIT_STAGE = "commit_stage";

/**
 * @brief Configuration for OCR pipeline.
 *
 * Uses the common ai_postprocess_pair_config_t pattern for configuring
 * the AI inference stage and postprocess stage.
 */
struct ocr_config_t : public ai_postprocess_pair_config_t
{
    // Inherits ai_config and post_config from ai_postprocess_pair_config_t
};

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
 * @brief Configuration for the quality gate that skips OCR for already-classified plates.
 */
struct quality_gate_config_t
{
    std::optional<int> ttl_seconds;                  ///< TTL for database entries in seconds (default: 30)
    std::optional<size_t> max_entries;               ///< Maximum database entries (default: 1000)
    std::optional<float> re_ocr_confidence_margin;   ///< If current detection confidence exceeds cached
                                                     ///< detection_confidence by this margin, re-run OCR (default: 0.2)
    std::optional<float> ocr_confidence_threshold;   ///< Minimum OCR mean confidence to accept a result (default: 0.8)
    std::optional<float> min_pixel_ratio;            ///< Minimum ratio of actual crop pixels to network input pixels.
                                                     ///< Crops below this ratio are skipped (default: 1.0)
    std::optional<float> max_aspect_ratio_deviation; ///< Maximum allowed deviation between actual and expected
                                                     ///< aspect ratios. Values above this are skipped (default: 0.5)
    std::optional<bool> require_lp_in_vehicle;       ///< If true, skip license plates not contained within any vehicle
                                                     ///< detection, and keep only the highest-confidence LP per vehicle
                                                     ///< (default: true)

    /**
     * @brief Merge configuration from another quality_gate_config_t.
     * @param other The configuration to merge from
     */
    void merge_from(const quality_gate_config_t &other);
};

/**
 * @brief Combined configuration for bbox crop + OCR pipeline.
 *
 * This structure combines bbox crop, OCR, and aggregator configurations
 * for a complete bbox crop + OCR + aggregation pipeline.
 */
struct bbox_crop_ocr_config_t
{
    bbox_crop_config_t bbox_crop_config;
    ocr_config_t ocr_config;
    aggregator_config_t aggregator_config;
    quality_gate_config_t quality_gate_config;

    /**
     * @brief Merge configuration from another bbox_crop_ocr_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const bbox_crop_ocr_config_t &other);

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
 * @brief Get default configuration for OCR pipeline only.
 *
 * @return ocr_config_t with sensible defaults
 */
ocr_config_t ocr_base_config();

/**
 * @brief Get default configuration for quality gate.
 *
 * @return quality_gate_config_t with sensible defaults
 */
quality_gate_config_t quality_gate_base_config();

/**
 * @brief Get default configuration for bbox crop + OCR pipeline.
 *
 * @return bbox_crop_ocr_config_t with sensible defaults
 */
bbox_crop_ocr_config_t base_config();

/**
 * @brief Generate an OCR pipeline with the given configuration.
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed OCR pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_ocr_pipeline(
    const std::string &pipeline_name = std::string(OCR_PIPELINE), std::optional<ocr_config_t> configs = std::nullopt);

/**
 * @brief Generate a bbox crop + OCR pipeline with the given configuration.
 *
 * Creates a pipeline where:
 * - BBox crop stage crops detected license plates from input
 * - OCR sub-pipeline (generated using generate_ocr_pipeline) processes each crop
 * - Aggregator combines the OCR results back together
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed bbox crop OCR pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_bbox_crop_ocr_pipeline(const std::string &pipeline_name = std::string(BBOX_CROP_OCR_PIPELINE),
                                std::optional<bbox_crop_ocr_config_t> configs = std::nullopt);

} // namespace hailo_analytics::analytics::license_plate_recognition
