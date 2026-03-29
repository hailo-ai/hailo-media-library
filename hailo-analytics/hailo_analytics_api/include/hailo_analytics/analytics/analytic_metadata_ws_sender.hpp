#pragma once

#include <optional>
#include <string>
#include <string_view>
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/sinks/websocket_sink_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "tl/expected.hpp"

namespace hailo_analytics::analytics::analytic_metadata_ws_sender
{

inline constexpr std::string_view ANALYTIC_METADATA_WS_SENDER_PIPELINE = "analytic_metadata_ws_sender_pipeline";

namespace codecs_stage = hailo_analytics::pipeline::codecs;
namespace sinks_stage = hailo_analytics::pipeline::sinks;

struct analytic_metadata_config_t
{
    std::optional<std::string> stage_name;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;
    std::optional<codecs_stage::Format> format;

    void merge_from(const analytic_metadata_config_t &other);
    void apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const;
};

analytic_metadata_config_t base_analytic_metadata_packager_config();

/**
 * @brief Configuration structure for WebSocket sink stages.
 *
 * Contains all configurable parameters for WebSocketSinkStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct websocket_config_t
{
    std::optional<std::string> stage_name;
    std::optional<uint16_t> port;
    std::optional<std::string> host;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<size_t> max_message_size;

    /**
     * @brief Merge configuration from another websocket_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const websocket_config_t &other);

    /**
     * @brief Apply this configuration to a WebSocketSinkStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(sinks_stage::WebSocketSinkStageBuild::Builder &b) const;
};

/**
 * @brief Combined configuration for analytic metadata packager and WebSocket sender pipeline.
 *
 * This structure combines analytic packager configuration with WebSocket sender configuration.
 */
struct analytic_metadata_ws_sender_config_t
{
    analytic_metadata_config_t analytic_metadata_config;
    websocket_config_t websocket_config;

    /**
     * @brief Merge configuration from another analytic_metadata_ws_sender_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const analytic_metadata_ws_sender_config_t &other);

    /**
     * @brief Apply analytic metadata packager configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const;

    /**
     * @brief Apply WebSocket sender configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(sinks_stage::WebSocketSinkStageBuild::Builder &b) const;
};

/**
 * @brief Get default configuration for WebSocket sender stage.
 *
 * @return websocket_config_t with sensible defaults
 */
websocket_config_t base_websocket_sender_config();

/**
 * @brief Get default configuration for analytic metadata WebSocket sender pipeline.
 *
 * Returns an analytic_metadata_ws_sender_config_t with default analytic metadata and
 * WebSocket sender configurations. Format defaults to JSON.
 *
 * @return analytic_metadata_ws_sender_config_t with sensible defaults
 */
analytic_metadata_ws_sender_config_t base_analytic_metadata_ws_sender_config();

/**
 * @brief Generate an analytic metadata WebSocket sender pipeline.
 *
 * Creates a pipeline with an analytic metadata packager stage (JSON format) as the first stage,
 * followed by a WebSocket sink stage.
 *
 * @param pipeline_name Name for the generated pipeline
 * @param user_configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_analytic_metadata_ws_sender_pipeline(
    const std::string &pipeline_name = std::string(ANALYTIC_METADATA_WS_SENDER_PIPELINE),
    std::optional<analytic_metadata_ws_sender_config_t> user_configs = std::nullopt);

} // namespace hailo_analytics::analytics::analytic_metadata_ws_sender
