#pragma once

/**
 * @file tee_stage.hpp
 * @brief Stage that sends the input data to multiple outputs (no copy).
 **/

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::routing
{

/**
 * @brief Stage that duplicates the input buffer to multiple outputs (no copy).
 */
class TeeStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    /**
     * @brief Construct a new TeeStage.
     *
     * @param name Stage name.
     * @param queue_size Input queue size.
     * @param leaky Whether the queue is leaky.
     * @param trace_processing_operations Whether to trace processing operations.
     */
    TeeStage(std::string name, size_t queue_size, bool leaky = false, bool trace_processing_operations = true);

    /**
     * @brief Process a single buffer and forward it to all outputs.
     *
     * @param data Input buffer.
     * @return AppStatus Status code.
     */
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder wrapper for TeeStage.
 */
class TeeStageBuild : public TeeStage
{
  public:
    /**
     * @brief TeeStage builder.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Stage name.
         * @return Builder reference.
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the stage queue size.
         * @param size Queue size.
         * @return Builder reference.
         */
        Builder &set_queue_size(size_t size);

        /**
         * @brief Enable/disable leaky queue behavior.
         * @param activate True to enable.
         * @return Builder reference.
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Enable/disable tracing.
         * @param activate True to enable.
         * @return Builder reference.
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build a TeeStage instance.
         * @return Shared pointer to the stage.
         */
        std::shared_ptr<TeeStage> buildptr() const;
    };

    /**
     * @brief Create a builder with default values.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
