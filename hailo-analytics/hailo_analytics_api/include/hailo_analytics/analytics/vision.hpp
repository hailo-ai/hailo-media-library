#pragma once

#include <optional>
#include <string>
#include <map>
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/pipeline.hpp"
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/utils/stream_utils.hpp"
#include "media_library/media_library.hpp"

namespace hailo_analytics::analytics::vision
{

inline std::string port_from_stream_id(const std::string &id, int base_port = 5000)
{
    return hailo_analytics::utils::port_from_stream_id(id, base_port);
}

} // namespace hailo_analytics::analytics::vision

#define PORT_FROM_ID(id, ...) hailo_analytics::utils::port_from_stream_id(id, ##__VA_ARGS__)

namespace hailo_analytics::analytics::vision
{

namespace codecs = hailo_analytics::pipeline::codecs;
namespace sinks = hailo_analytics::pipeline::sinks;
namespace sources = hailo_analytics::pipeline::sources;

/**
 * @brief Configuration structure for frontend stages.
 *
 * Contains all configurable parameters for FrontendStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct frontend_config_t
{
    std::optional<std::string> stage_name;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;

    /**
     * @brief Merge configuration from another frontend_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const frontend_config_t &other);

    /**
     * @brief Apply this configuration to a FrontendStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(sources::FrontendStageBuild::Builder &b) const;
};

/**
 * @brief Configuration structure for encoder stages.
 *
 * Contains all configurable parameters for EncoderStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct encoder_config_t
{
    std::optional<std::string> stage_name;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;

    /**
     * @brief Merge configuration from another encoder_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const encoder_config_t &other);

    /**
     * @brief Apply this configuration to an EncoderStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(codecs::EncoderStageBuild::Builder &b) const;
};

/**
 * @brief Configuration structure for UDP sink stages.
 *
 * Contains all configurable parameters for UdpStage builders.
 * All parameters are optional to allow flexible configuration.
 */
struct udp_config_t
{
    // UDP stage parameters
    std::optional<std::string> stage_name;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> print_fps;
    std::optional<bool> trace;

    // UDP configure() parameters
    std::optional<std::string> host;
    std::optional<std::string> port;
    std::optional<hailo_analytics::pipeline::sinks::EncodingType> encoding_type;

    /**
     * @brief Merge configuration from another udp_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const udp_config_t &other);

    /**
     * @brief Apply this configuration to a UdpStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(sinks::UdpStageBuild::Builder &b) const;
};

/**
 * @brief Combined configuration for encoder and UDP output stages.
 *
 * This is a common pattern where encoded data is sent over UDP.
 * This structure combines both configurations.
 */
struct vision_output_config_t
{
    encoder_config_t encoder_config;
    udp_config_t udp_config;

    /**
     * @brief Merge configuration from another vision_output_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const vision_output_config_t &other);

    /**
     * @brief Apply encoder configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(codecs::EncoderStageBuild::Builder &b) const;

    /**
     * @brief Apply UDP configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(sinks::UdpStageBuild::Builder &b) const;
};

/**
 * @brief Complete vision pipeline configuration.
 *
 * Represents a full vision pipeline with one frontend source and multiple outputs.
 * Each output consists of an encoder + UDP sink pair mapped by stream ID.
 */
struct vision_config_t
{
    frontend_config_t frontend_config;
    std::map<output_stream_id_t, vision_output_config_t> outputs;

    /**
     * @brief Merge configuration from another vision_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const vision_config_t &other);

    /**
     * @brief Apply frontend configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(sources::FrontendStageBuild::Builder &b) const;

    /**
     * @brief Apply encoder configuration for a specific output stream to a builder.
     *
     * @param b The builder to configure
     * @param stream_id The stream ID of the output
     */
    void apply_to(codecs::EncoderStageBuild::Builder &b, const output_stream_id_t &stream_id) const;

    /**
     * @brief Apply UDP configuration for a specific output stream to a builder.
     *
     * @param b The builder to configure
     * @param stream_id The stream ID of the output
     */
    void apply_to(sinks::UdpStageBuild::Builder &b, const output_stream_id_t &stream_id) const;
};

/**
 * @brief Get default configuration for vision output pipeline.
 *
 * @return vision_output_config_t with sensible defaults
 */
vision_output_config_t base_vision_output_config(std::string output_stream_id = "sink0", int base_port = 5000);

/**
 * @brief Get default configuration for complete vision pipeline.
 *
 * Returns a vision_config_t with default frontend configuration and one output.
 *
 * @return vision_config_t with sensible defaults
 */
vision_config_t base_vision_config(std::vector<frontend_output_stream_t> frontend_streams, int base_port = 5000);

/**
 * @brief Generate a vision output pipeline (encoder -> UDP) with the given configuration.
 *
 * Creates a pipeline with an encoder stage connected to a UDP sink stage.
 * Configures the encoder stage with the provided MediaLibrary and stream ID.
 *
 * @param media_library Shared pointer to the MediaLibrary instance
 * @param stream_id The output stream ID for the encoder
 * @param pipeline_name Name for the generated pipeline
 * @param user_configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed vision output pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_vision_output_pipeline(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id,
                                const std::string &pipeline_name = "vision_output_pipeline",
                                std::optional<vision_output_config_t> user_configs = std::nullopt);

/**
 * @brief Generate a complete vision pipeline with the given configuration.
 *
 * Creates a pipeline with a frontend source connected to multiple encoder->UDP output pairs.
 * Configures the frontend and all encoder stages with the provided MediaLibrary instance.
 *
 * @param media_library Shared pointer to the MediaLibrary instance
 * @param pipeline_name Name for the generated pipeline
 * @param user_configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed vision pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus> generate_vision_pipeline(
    MediaLibraryInterfacePtr media_library, const std::string &pipeline_name = "vision_pipeline",
    std::optional<vision_config_t> user_configs = std::nullopt);

} // namespace hailo_analytics::analytics::vision
