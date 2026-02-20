#pragma once

/**
 * @file demuxer_stage.hpp
 * @brief Stage that demultiplexes a single video stream into multiple output streams.
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
 * @brief Stage that demultiplexes a single video stream into multiple output streams.
 *
 * The DemuxerStage processes buffers containing embedded sub-buffers (stored in BufferMetadata)
 * and separates them into two distinct output streams: a main outlet and a sub outlet.
 * This is useful for scenarios where a single buffer contains multiple data streams that need
 * to be processed separately.
 *
 * The stage extracts sub-buffers from BufferMetadata within the incoming buffer and sends them
 * to the sub outlet, while the main buffer (with metadata removed) is sent to the main outlet.
 * Optionally, HailoROI metadata can be copied from the main buffer to sub-buffers.
 */
class DemuxerStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::string m_main_outlet_name; ///< Name of the main output stream
    std::string m_sub_outlet_name;  ///< Name of the sub output stream
    bool m_copy_roi_metadata;       ///< Flag to enable copying of HailoROI metadata to sub-buffers

  public:
    /**
     * @brief Constructs a DemuxerStage.
     *
     * @param name The name of the stage
     * @param main_outlet_name The name of the main output outlet
     * @param sub_outlet_name The name of the sub output outlet
     * @param queue_size The size of the internal processing queue (default: 10)
     * @param leaky If true, drops oldest buffers when queue is full (default: false)
     * @param trace_processing_operations Enable tracing for performance analysis (default: true)
     * @param copy_roi_metadata If true, copies HailoROI metadata from main to sub buffers (default: false)
     */
    DemuxerStage(std::string name, std::string main_outlet_name, std::string sub_outlet_name, size_t queue_size = 10,
                 bool leaky = false, bool trace_processing_operations = true, bool copy_roi_metadata = false);

    /**
     * @brief Processes incoming buffers by demultiplexing them into main and sub streams.
     *
     * Extracts BufferMetadata from the incoming buffer, sends the contained sub-buffer to
     * the sub outlet, removes the metadata from the main buffer, and sends the main buffer
     * to the main outlet. If copy_roi_metadata is enabled, copies HailoROI objects from
     * the main buffer to the sub-buffer before sending.
     *
     * @param buffer The input buffer to process
     * @return AppStatus::SUCCESS on successful processing
     */
    AppStatus process(BufferPtr buffer) override;

  private:
    /**
     * @brief Copies HailoROI metadata from the main buffer to the sub-buffer.
     *
     * @param main_buffer The source buffer containing HailoROI metadata
     * @param sub_buffer The destination buffer to receive the copied metadata
     */
    void copy_hailo_roi_metadata(BufferPtr main_buffer, BufferPtr sub_buffer);
};

/**
 * @brief Builder class for constructing DemuxerStage instances.
 *
 * Provides a fluent interface for configuring and building DemuxerStage objects
 * with various options. This follows the builder pattern to ensure all required
 * parameters are set before construction.
 */
class DemuxerStageBuild : public DemuxerStage
{
  public:
    /**
     * @brief Builder for DemuxerStage configuration.
     *
     * Allows step-by-step configuration of a DemuxerStage before building.
     * Required parameters: stage name, main outlet name, and sub outlet name.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_main_outlet_name;
        std::optional<std::string> m_sub_outlet_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_trace = true;
        bool m_copy_roi_metadata = false;

      public:
        /**
         * @brief Sets the stage name (required).
         * @param name The name for the demuxer stage
         * @return Reference to this builder for method chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Sets the main outlet name (required).
         * @param name The name for the main output outlet
         * @return Reference to this builder for method chaining
         */
        Builder &set_main_outlet_name(std::string name);

        /**
         * @brief Sets the sub outlet name (required).
         * @param name The name for the sub output outlet
         * @return Reference to this builder for method chaining
         */
        Builder &set_sub_outlet_name(std::string name);

        /**
         * @brief Sets the internal queue size.
         * @param size The size of the processing queue (default: 10)
         * @return Reference to this builder for method chaining
         */
        Builder &set_queue_size(size_t size);

        /**
         * @brief Configures the leaky queue behavior.
         * @param leaky If true, drops oldest buffers when queue is full (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_leaky_opt(bool leaky);

        /**
         * @brief Enables or disables tracing for performance analysis.
         * @param activate If true, enables tracing (default: true)
         * @return Reference to this builder for method chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Configures HailoROI metadata copying.
         * @param copy_roi If true, copies ROI metadata from main to sub buffers (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_copy_roi_metadata_opt(bool copy_roi);

        /**
         * @brief Builds and returns a shared pointer to the configured DemuxerStage.
         * @return Shared pointer to the newly created DemuxerStage
         * @throws std::invalid_argument if required parameters are not set
         */
        std::shared_ptr<DemuxerStage> buildptr() const;
    };

    /**
     * @brief Creates a new Builder instance for constructing a DemuxerStage.
     * @return A new Builder instance
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::muxing
