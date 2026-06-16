#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <tuple>
#include <utility>

#include "pipeline.hpp"
#include "hailo_analytics/pipeline/sources/frontend_stage.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline
{
using FrontendStage = hailo_analytics::pipeline::sources::FrontendStage;

/**
 * @brief Builder class for constructing pipelines with automatic validation.
 *
 * PipelineBuilder provides a fluent interface for building pipelines with compile-time and runtime
 * validation. It allows flexible interleaving of stage additions and connections to support
 * complex pipeline topologies, including pipelines that contain other pipelines as stages.
 *
 * **Key Features:**
 * - Fluent API with method chaining
 * - Automatic stage name extraction from Stage objects
 * - Type-safe stage addition with compile-time checking
 * - Validates all stages are used in connections at build time
 * - Supports both generic connections and frontend multi-stream connections
 * - Single-stage pipelines require no connections
 * - Flexible: supports interleaved add/connect operations
 *
 * **Typical Usage Pattern:**
 * ```cpp
 * PipelineBuilder builder;
 * builder.add_stage(inference_stage)
 *        .add_stage(postprocess_stage)
 *        .connect("inference", "postprocess")
 *        .build("my_pipeline");
 * ```
 */
class PipelineBuilder
{
  public:
    /**
     * @brief Adds a stage to the pipeline with explicit name and type.
     * @tparam T The stage type (must derive from Stage)
     * @param name The name to register this stage under
     * @param stage Shared pointer to the stage
     * @param type The stage category (SOURCE, GENERAL, or SINK)
     * @return Reference to this builder for method chaining
     * @throws std::invalid_argument if stage is null or name already exists
     *
     * The template parameter is validated at compile-time to ensure it derives from Stage.
     * Stage names must be unique within a pipeline.
     * Can be called at any time before build().
     */
    PipelineBuilder &add_stage(const std::string &name, StagePtr stage, StageType type = StageType::GENERAL);

    /**
     * @brief Adds a stage to the pipeline using the stage's own name.
     * @param stage Shared pointer to the stage
     * @param type The stage category (SOURCE, GENERAL, or SINK)
     * @return Reference to this builder for method chaining
     * @throws std::invalid_argument if stage is null or stage name already exists
     *
     * The stage name is extracted using stage->get_name().
     * This is the preferred method when the stage already has a meaningful name.
     * Can be called at any time before build().
     */
    PipelineBuilder &add_stage(StagePtr stage, StageType type = StageType::GENERAL);

    /**
     * @brief Connects a source stage's output to a target stage's input.
     * @param sourceName The name of the stage sending buffers
     * @param targetName The name of the stage receiving buffers
     * @return Reference to this builder for method chaining
     * @throws std::invalid_argument if either stage name doesn't exist
     *
     * Uses the standard add_subscriber mechanism for generic stage connections.
     * Can be called at any time before build(), allowing flexible interleaving with add_stage().
     */
    PipelineBuilder &connect(const std::string &sourceName, const std::string &targetName);

    /**
     * @brief Connects a source stage to a target stage with a stream-id key.
     * @param sourceName The name of the stage sending buffers
     * @param streamId The stream identifier the source can use to dispatch to this subscriber
     * @param targetName The name of the stage receiving buffers
     * @return Reference to this builder for method chaining
     * @throws std::invalid_argument if either stage name doesn't exist
     *
     * Same as the 2-arg connect, but also passes streamId to the source's add_subscriber so
     * the source can dispatch by stream id (e.g. SplitStreamsStage routes carrier and passengers
     * to per-stream subscribers). The 2-arg connect stays the right call for stages that don't
     * care about per-subscriber routing.
     * Can be called at any time before build(), allowing flexible interleaving with add_stage().
     */
    PipelineBuilder &connect(const std::string &sourceName, const std::string &streamId, const std::string &targetName);

    /**
     * @brief Connects a FrontendStage's specific stream output to a target stage.
     * @param frontendName The name of the FrontendStage
     * @param streamId The stream identifier string
     * @param targetName The name of the target stage receiving this stream
     * @return Reference to this builder for method chaining
     * @throws std::invalid_argument if either stage name doesn't exist
     *
     * FrontendStages can produce multiple output streams. This method connects a specific
     * stream to a target stage.
     * Can be called at any time before build(), allowing flexible interleaving with add_stage().
     */
    PipelineBuilder &connect_frontend(const std::string &frontendName, const std::string &streamId,
                                      const std::string &targetName);

    /**
     * @brief Builds and returns the configured pipeline.
     * @param name The name for the created pipeline
     * @param trace_processing_operations If true, enables performance tracing
     * @return Shared pointer to the constructed Pipeline
     *
     * **Build Process:**
     * 1. Creates Pipeline and adds all stages with their types
     * 2. Establishes all generic connections
     * 3. Establishes all frontend subscriptions
     *
     * The builder supports flexible pipeline topologies including:
     * - Single-stage pipelines (no connections required)
     * - Multi-stage connected pipelines
     * - Pipelines with disconnected stages (if needed by the application)
     * - Pipelines containing other pipelines as stages
     */
    std::shared_ptr<Pipeline> build(std::string name, bool trace_processing_operations = true);

    /**
     * @brief Export the pipeline structure to a DOT file for visualization.
     * @param filename Path to the output DOT file
     * @return Reference to this PipelineBuilder for method chaining
     *
     * Creates a GraphViz DOT file representing the pipeline structure.
     */
    PipelineBuilder &export_to_dot(const std::string &filename);

  private:
    std::unordered_map<std::string, StagePtr> m_allStages;
    std::vector<std::pair<std::string, std::string>> m_connections;
    // (sourceName, streamId, targetName) — stream-id-keyed connections registered via the
    // 3-arg connect overload. Wired separately at build time via add_subscriber(target, streamId).
    std::vector<std::tuple<std::string, std::string, std::string>> m_streamIdConnections;
    std::vector<std::tuple<std::string, std::string, std::string>> m_frontendSubscriptions;
    std::unordered_map<std::string, StageType> m_stageTypes;

    void validate_and_add_stage(const std::string &name, StagePtr stage, StageType type = StageType::GENERAL);

    // Log diagnostic warnings for potentially unintended pipeline configurations
    void log_diagnostics() const;
};

} // namespace hailo_analytics::pipeline
