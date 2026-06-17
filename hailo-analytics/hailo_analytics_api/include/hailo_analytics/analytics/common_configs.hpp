#pragma once

#include <hailort.h> // IWYU pragma: keep
#include <stdint.h>
#include <hailo_gst_tensor_metadata.hpp>
/**
 * @file common_configs.hpp
 * @brief Common configuration structures for analytics pipelines.
 **/
#include <optional>
#include <chrono>
#include <string>
#include <cstddef>
#include <vector>

#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/ai/postprocess_stage.hpp"
#include "hailo_analytics/pipeline/cropping/aggregator_stage.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::analytics
{

namespace ai_stages = hailo_analytics::pipeline::ai;
namespace cropping_stages = hailo_analytics::pipeline::cropping;

/**
 * @brief Common configuration structure for AI inference stages.
 *
 * Contains all configurable parameters for HailortAsyncStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct ai_stage_config_t
{
    std::optional<std::string> stage_name;
    std::optional<std::string> hef_path;
    std::optional<size_t> queue_size;
    std::optional<int> output_pool_size;
    std::optional<std::string> group_id;
    std::optional<int> batch_size;
    std::optional<size_t> job_limit;
    std::optional<int> scheduler_threshold;
    std::optional<bool> dynamic_threshold;
    std::optional<std::chrono::milliseconds> scheduler_timeout;
    std::optional<uint8_t> scheduler_priority;
    std::optional<hailo_analytics::pipeline::StagePoolMode> pool_mode;
    std::optional<float32_t> nms_score_threshold;
    std::optional<std::vector<bool>> nms_classes_filter_mask;
    std::optional<size_t> nms_max_accumulated_mask_size_multiplier;
    std::optional<bool> use_hailort_service;
    std::optional<bool> trace;

    /**
     * @brief Merge configuration from another ai_stage_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const ai_stage_config_t &other);

    /**
     * @brief Apply this configuration to a HailortAsyncStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(ai_stages::HailortAsyncStageBuild::Builder &b) const;
};

/**
 * @brief Common configuration structure for postprocess stages.
 *
 * Contains all configurable parameters for PostprocessStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct postprocess_stage_config_t
{
    std::optional<std::string> stage_name;
    std::optional<std::string> so_path;
    std::optional<std::string> function_name;
    std::optional<std::string> config_path;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;

    /**
     * @brief Merge configuration from another postprocess_stage_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const postprocess_stage_config_t &other);

    /**
     * @brief Apply this configuration to a PostprocessStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(ai_stages::PostprocessStageBuild::Builder &b) const;
};

/**
 * @brief Combined configuration for an AI stage and its postprocess stage.
 *
 * This is a common pattern where an AI inference stage is followed by
 * a postprocessing stage. This structure combines both configurations.
 */
struct ai_postprocess_pair_config_t
{
    ai_stage_config_t ai_config;
    postprocess_stage_config_t post_config;

    /**
     * @brief Merge configuration from another ai_postprocess_pair_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const ai_postprocess_pair_config_t &other);

    /**
     * @brief Apply AI stage configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(ai_stages::HailortAsyncStageBuild::Builder &b) const;

    /**
     * @brief Apply postprocess stage configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(ai_stages::PostprocessStageBuild::Builder &b) const;
};

/**
 * @brief Configuration structure for aggregator stages.
 *
 * Contains all configurable parameters for AggregatorStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct aggregator_config_t
{
    std::optional<std::string> stage_name;
    std::optional<int> static_sub_frames;
    std::optional<std::string> main_inlet_name;
    std::optional<size_t> main_queue_size;
    std::optional<bool> main_queue_leaky;
    std::optional<std::string> sub_inlet_name;
    std::optional<size_t> sub_queue_size;
    std::optional<bool> sub_queue_leaky;
    std::optional<bool> multi_scale;
    std::optional<bool> skip_migration;
    std::optional<bool> trace;
    std::optional<float> iou_threshold;
    std::optional<float> border_threshold;
    std::optional<bool> copy_sub_frame_tensor_to_metadata;

    /**
     * @brief Merge configuration from another aggregator_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const aggregator_config_t &other);

    /**
     * @brief Apply this configuration to an AggregatorStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(cropping_stages::AggregatorStageBuild::Builder &b) const;
};

} // namespace hailo_analytics::analytics
