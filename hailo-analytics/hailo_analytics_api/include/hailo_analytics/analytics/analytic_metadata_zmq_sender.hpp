#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <functional>
#include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
#include "hailo_analytics/pipeline/codecs/analytic_metadata_packager_stage.hpp"
#include "hailo_analytics/pipeline/sinks/zmq_comm_stage.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "tl/expected.hpp"

namespace hailo_analytics::analytics::analytic_metadata_zmq_sender
{

namespace codecs_stage = hailo_analytics::pipeline::codecs;
namespace sinks_stage = hailo_analytics::pipeline::sinks;

struct analytic_metadata_config_t
{
    std::optional<std::string> stage_name;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;

    void merge_from(const analytic_metadata_config_t &other);
    void apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const;
};

analytic_metadata_config_t base_analytic_metadata_packager_config();

/**
 * @brief Configuration structure for zeromq stages.
 *
 * Contains all configurable parameters for zeromq builders.
 * All parameters are optional to allow flexible configuration.
 */
struct zeromq_config_t
{
    std::optional<std::string> stage_name;
    std::optional<size_t> queue_size;
    std::optional<bool> leaky;
    std::optional<bool> trace;
    std::optional<bool> print_fps;
    std::optional<sinks_stage::ZmqCommStage::Mode> mode;
    std::optional<std::string> pub_address;
    std::optional<std::string> sub_address;

    /**
     * @brief Merge configuration from another zeromq_config_t.
     * Non-empty optional values from 'other' will override this config's values.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const zeromq_config_t &other);

    /**
     * @brief Apply this configuration to an ZmqCommStageBuild::Builder.
     * Only non-empty optional values will be applied.
     *
     * @param b The builder to configure
     */
    void apply_to(sinks_stage::ZmqCommStageBuild::Builder &b) const;
};

/**
 * @brief Combined configuration for analytic metadata packager and zeromq sender pipeline.
 *
 * This structure combines analytic packager configuration with zero mq sender configuration pipeline
 */
struct analytic_metadata_zmq_sender_config_t
{
    analytic_metadata_config_t analytic_metadata_config;
    zeromq_config_t zeromq_config;

    /**
     * @brief Merge configuration from another analytic_metadata_zmq_sender_config_t.
     *
     * @param other The configuration to merge from
     */
    void merge_from(const analytic_metadata_zmq_sender_config_t &other);

    /**
     * @brief Apply analytic medata packager configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(codecs_stage::AnalyticMetadataPackagerStageBuild::Builder &b) const;

    /**
     * @brief Apply zeromq sender configuration to a builder.
     *
     * @param b The builder to configure
     */
    void apply_to(sinks_stage::ZmqCommStageBuild::Builder &b) const;
};

/**
 * @brief Get default configuration for zero mq sender stage.
 *
 * @return zeromq_config_t with sensible defaults
 */
zeromq_config_t base_zeromq_sender_config();

/**
 * @brief Get default configuration for analytic metadata zmq sender pipeline.
 *
 * Returns an analytic_metadata_zmq_sender_config_t with default analytic metadata and
 * zeromq sender configurations.
 *
 * @return analytic_metadata_zmq_sender_config_t with sensible defaults
 */
analytic_metadata_zmq_sender_config_t base_analytic_metadata_zmq_sender_config();

/**
 * @brief Generate an analytic metadata zmq sender pipeline.
 *
 * Creates a pipeline with an analytic metadata stage as the first stage, followed by
 * a zeromq stage pipeline.
 *
 * @param pipeline_name Name for the generated pipeline
 * @param user_configs Optional user-provided configuration (will be merged with defaults if provided)
 * @return Expected<PipelinePtr, AppStatus> The constructed pipeline or error status
 */
tl::expected<hailo_analytics::pipeline::PipelinePtr, hailo_analytics::pipeline::AppStatus>
generate_analytic_metadata_zmq_sender_pipeline(
    const std::string &pipeline_name = "analytic_metadata_zmq_sender_pipeline",
    std::optional<analytic_metadata_zmq_sender_config_t> user_configs = std::nullopt);

} // namespace hailo_analytics::analytics::analytic_metadata_zmq_sender
