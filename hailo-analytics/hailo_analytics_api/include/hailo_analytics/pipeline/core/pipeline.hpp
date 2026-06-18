#pragma once

// general includes
#include <vector>
#include <memory>
#include <optional>
#include <string>

// infra includes
#include "stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

namespace hailo_analytics::pipeline
{

/**
 * @brief Categorization of stages within a pipeline.
 *
 * Stage types help the Pipeline manage startup and shutdown ordering.
 * Source stages are started last and stopped first to control data flow.
 */
enum class StageType
{
    GENERAL = 0, ///< General processing stage (e.g., inference, post-processing)
    SOURCE,      ///< Source stage that generates or captures data (e.g., camera, file reader)
    SINK         ///< Sink stage that outputs or consumes data (e.g., encoder, display, network sender)
};

class Pipeline;

using PipelinePtr = std::shared_ptr<Pipeline>;

/**
 * @brief Container and manager for a collection of connected stages.
 *
 * The Pipeline class organizes multiple Stage objects into a processing chain and manages
 * their lifecycle. It provides:
 * - Automatic startup/shutdown ordering based on stage types
 * - Stage registration and retrieval by name
 * - Input/output stage designation for inter-pipeline connections
 * - The ability to treat a pipeline as a single stage (to encapsulate complex processing)
 *
 * Pipelines can be nested, allowing complex multi-pipeline systems where entire pipelines
 * act as stages within a larger pipeline graph.
 *
 * Stage Ordering:
 * - **Start Order**: Sinks → General → Sources (downstream to upstream)
 * - **Stop Order**: Sources → General → Sinks (upstream to downstream)
 *
 * This ordering ensures that:
 * - Downstream stages are ready before upstream stages start producing data
 * - Upstream stages stop producing data before downstream stages shut down
 */
class Pipeline : public Stage
{
  private:
    std::vector<StagePtr> m_stages;      // All stages, used for full queries (get and print)
    std::vector<StagePtr> m_gen_stages;  // For general type stages
    std::vector<StagePtr> m_src_stages;  // For source type stages
    std::vector<StagePtr> m_sink_stages; // For sink type stages

    StagePtr m_in_stage;  // The stage that will subscribe to external sources
    StagePtr m_out_stage; // The stage that will publish to external sinks

  public:
    /**
     * @brief Constructs a Pipeline with the given name.
     * @param name The name of the pipeline for identification and logging
     * @param trace_processing_operations If true, enables performance tracing for the pipeline
     */
    Pipeline(std::string name, bool trace_processing_operations = true);

    /**
     * @brief Adds a stage to the pipeline with the specified type.
     * @param stage The stage to add to the pipeline
     * @param type The category of the stage (GENERAL, SOURCE, or SINK)
     *
     * Stages are organized by type to control startup and shutdown ordering.
     * All stages are also added to the main stages list for lookup and management.
     */
    void add_stage(StagePtr stage, StageType type = StageType::GENERAL);

    /**
     * @brief Sets the input stage of the pipeline.
     * @param stage The stage that will receive data from external sources
     *
     * When the pipeline is connected to another stage or pipeline, this stage
     * will be the entry point for incoming buffers.
     */
    void set_in_stage(StagePtr stage);

    /**
     * @brief Sets the output stage of the pipeline.
     * @param stage The stage that will send data to external sinks
     *
     * When the pipeline has subscribers, this stage will push buffers to them.
     */
    void set_out_stage(StagePtr stage);

    // Overrides
    /**
     * @brief Starts all stages in the pipeline in the correct order.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Stages are started in this order:
     * 1. Sink stages (downstream consumers)
     * 2. General stages (middle processing)
     * 3. Source stages (upstream producers)
     *
     * This ensures downstream stages are ready before upstream stages start producing data.
     */
    AppStatus start() override;

    /**
     * @brief Stops all stages in the pipeline in the correct order.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Stages are stopped in this order:
     * 1. Source stages (stop producing data)
     * 2. General stages (finish processing)
     * 3. Sink stages (finish consuming data)
     *
     * This ensures data flow stops cleanly from upstream to downstream.
     */
    AppStatus stop() override;

    /**
     * @brief Subscribes another stage to this pipeline's output.
     * @param subscriber The stage that will receive buffers from this pipeline
     * @param stream_id Optional stream identifier for multi-stream scenarios
     *
     * Delegates to the output stage's add_subscriber method. The subscriber will
     * receive buffers from the pipeline's designated output stage.
     */
    void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) override;

    /**
     * @brief Creates an input queue for receiving buffers from a publisher.
     * @param publisher_name The name of the stage that will push buffers to this pipeline
     *
     * Delegates to the input stage's add_queue method.
     */
    void add_queue(std::string publisher_name) override;

    /**
     * @brief Pushes a buffer to this pipeline's input stage.
     * @param data The buffer to push
     * @param publisher_name The name of the publisher stage sending the buffer
     *
     * Delegates to the input stage's push method.
     */
    void push(BufferPtr data, std::string publisher_name) override;

    /**
     * @brief Retrieves a stage from the pipeline by name.
     * @param stage_name The name of the stage to find
     * @return Shared pointer to the stage if found, nullptr otherwise
     *
     * Searches through all stages in the pipeline and returns the first match.
     * Useful for runtime configuration or inspection of pipeline components.
     */
    StagePtr get_stage_by_name(std::string stage_name);

    /**
     * @brief Get all stages in the pipeline.
     * @return Vector of all stages
     */
    const std::vector<StagePtr> &get_stages() const;

    /**
     * @brief Get the input stage of the pipeline.
     * @return Shared pointer to the input stage, or nullptr if not set
     */
    StagePtr get_in_stage() const;

    /**
     * @brief Get the output stage of the pipeline.
     * @return Shared pointer to the output stage, or nullptr if not set
     */
    StagePtr get_out_stage() const;
};

} // namespace hailo_analytics::pipeline
