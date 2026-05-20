#pragma once

#include <string_view>
#include <optional>
#include <string>

#include "hailo_analytics/analytics/common_configs.hpp"
#include "tl/expected.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::analytics::detection
{

// Detection pipeline parameters
inline constexpr std::string_view DETECTION_PIPELINE = "detection_pipeline";

inline constexpr std::string_view DETECTION_STAGE = "detection_stage";
inline constexpr std::string_view DETECTION_GROUP_ID = "device0";

inline constexpr std::string_view DETECTION_POST_STAGE = "detection_post";
inline constexpr std::string_view DETECTION_POST_SO = "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so";

/**
 * @brief Configuration for detection pipeline.
 *
 * Uses the common ai_postprocess_pair_config_t pattern for configuring
 * the AI inference stage and postprocess stage.
 */
struct detection_config_t : public ai_postprocess_pair_config_t
{
    // Inherits ai_config and post_config from ai_postprocess_pair_config_t
};

/**
 * @brief Get default configuration for detection pipeline.
 *
 * @return detection_config_t with sensible defaults
 */
detection_config_t base_config();

/**
 * @brief Generate a detection pipeline with the given configuration.
 *
 * @param pipeline_name Name for the generated pipeline
 * @param configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed detection pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_detection_pipeline(
    const std::string &pipeline_name = std::string(DETECTION_PIPELINE),
    std::optional<detection_config_t> configs = std::nullopt);

} // namespace hailo_analytics::analytics::detection
