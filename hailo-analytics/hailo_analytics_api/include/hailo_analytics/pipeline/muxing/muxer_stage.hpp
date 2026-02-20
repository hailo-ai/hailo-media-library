#pragma once

/**
 * @file muxer_stage.hpp
 * @brief Stage that multiplexes multiple video streams into a single output stream.
 **/

// General includes
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>
#include <cstddef>
#include <atomic>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::muxing
{

/**
 * @brief Stage that multiplexes multiple video streams into a single output stream.
 *
 * The MuxerStage combines two separate input streams (main and sub) into a single output stream
 * by embedding the sub-stream buffer as metadata (BufferMetadata) within the main stream buffer.
 * This is useful for scenarios where multiple related video streams need to be synchronized and
 * processed together in the pipeline.
 *
 * The stage maintains two separate input queues (main and sub) and blocks on both, ensuring that
 * buffers from each stream are properly paired before being sent downstream. The sub-buffer is
 * attached to the main buffer as metadata, allowing downstream stages to access both streams.
 */
class MuxerStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::string m_main_inlet_name; ///< Name of the main input stream
    size_t m_main_queue_size;      ///< Size of the main input queue
    std::string m_sub_inlet_name;  ///< Name of the sub input stream
    size_t m_sub_queue_size;       ///< Size of the sub input queue

  public:
    /**
     * @brief Constructs a MuxerStage.
     *
     * @param name The name of the stage
     * @param main_inlet_name The name of the main input inlet
     * @param main_queue_size The size of the main input queue
     * @param main_queue_leaky If true, drops oldest buffers when main queue is full
     * @param sub_inlet_name The name of the sub input inlet
     * @param sub_queue_size The size of the sub input queue
     * @param sub_queue_leaky If true, drops oldest buffers when sub queue is full
     * @param trace_processing_operations Enable tracing for performance analysis (default: true)
     */
    MuxerStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
               std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky,
               bool trace_processing_operations = true);

    /**
     * @brief Adds a queue to the stage (no-op for MuxerStage).
     *
     * MuxerStage has fixed queues (main and sub) that are created in the constructor,
     * so this method does nothing.
     *
     * @param name The name of the queue (ignored)
     */
    void add_queue(std::string name) override;

    /**
     * @brief Main processing loop that multiplexes main and sub streams.
     *
     * Continuously reads from both main and sub queues, waits for buffers from each stream,
     * embeds the sub-buffer as metadata in the main buffer, and sends the combined buffer
     * to subscribers. Stops when either stream reaches end-of-stream.
     */
    void loop() override;
};

/**
 * @brief Builder class for constructing MuxerStage instances.
 *
 * Provides a fluent interface for configuring and building MuxerStage objects
 * with various options. This follows the builder pattern to ensure all required
 * parameters are set before construction.
 */
class MuxerStageBuild : public MuxerStage
{
  public:
    /**
     * @brief Builder for MuxerStage configuration.
     *
     * Allows step-by-step configuration of a MuxerStage before building.
     * Required parameters: stage name, main inlet name, and sub inlet name.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_main_inlet_name;
        size_t m_main_queue_size = 10;
        bool m_main_queue_leaky = false;
        std::optional<std::string> m_sub_inlet_name;
        size_t m_sub_queue_size = 10;
        bool m_sub_queue_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Sets the stage name (required).
         * @param name The name for the muxer stage
         * @return Reference to this builder for method chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Sets the main inlet name (required).
         * @param name The name for the main input inlet
         * @return Reference to this builder for method chaining
         */
        Builder &set_main_inlet_name(std::string name);

        /**
         * @brief Sets the main queue size.
         * @param size The size of the main input queue (default: 10)
         * @return Reference to this builder for method chaining
         */
        Builder &set_main_queue_size(size_t size);

        /**
         * @brief Configures the main queue leaky behavior.
         * @param leaky If true, drops oldest buffers when main queue is full (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_main_leaky(bool leaky);

        /**
         * @brief Sets the sub inlet name (required).
         * @param name The name for the sub input inlet
         * @return Reference to this builder for method chaining
         */
        Builder &set_sub_inlet_name(std::string name);

        /**
         * @brief Sets the sub queue size.
         * @param size The size of the sub input queue (default: 10)
         * @return Reference to this builder for method chaining
         */
        Builder &set_sub_queue_size(size_t size);

        /**
         * @brief Configures the sub queue leaky behavior.
         * @param leaky If true, drops oldest buffers when sub queue is full (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_sub_leaky(bool leaky);

        /**
         * @brief Enables or disables tracing for performance analysis.
         * @param activate If true, enables tracing (default: true)
         * @return Reference to this builder for method chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Builds and returns a shared pointer to the configured MuxerStage.
         * @return Shared pointer to the newly created MuxerStage
         * @throws std::invalid_argument if required parameters are not set
         */
        std::shared_ptr<MuxerStage> buildptr() const;
    };

    /**
     * @brief Creates a new Builder instance for constructing a MuxerStage.
     * @return A new Builder instance
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::muxing
