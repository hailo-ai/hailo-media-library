#pragma once

/**
 * @file valve_stage.hpp
 * @brief Stage that can block or allow buffers to pass through.
 **/

// General includes
#include <atomic>
#include <memory>
#include <string>
#include <optional>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

#define VALVE_QUEUE_SIZE_DEFAULT (1)

namespace hailo_analytics::pipeline::routing
{

/**
 * @brief Class representing a valve stage in the pipeline.
 *
 * This stage can block or allow buffers to pass through based on a valve state.
 * When the valve is open (true), buffers pass through to subscribers.
 * When the valve is closed (false), buffers are dropped.
 * Typically used as a GENERAL stage in the middle of a pipeline.
 */
class ValveStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::atomic<bool> m_valve; ///< Valve state: true = open, false = closed.

  public:
    /**
     * @brief Construct a new ValveStage object.
     *
     * @param name Name of the stage.
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     */
    ValveStage(std::string name, size_t queue_size = VALVE_QUEUE_SIZE_DEFAULT, bool leaky = false,
               bool trace_processing_operations = true);

    /**
     * @brief Process a buffer by either passing it through or dropping it.
     *
     * @param data Buffer to process.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data) override;

    /**
     * @brief Set the valve state.
     *
     * @param valve True to open the valve (allow buffers), false to close (block buffers).
     */
    void set_valve(bool valve);
};

/**
 * @brief Builder-based valve stage for simplified construction.
 *
 * Provides a builder pattern interface for creating valve stages with
 * configurable parameters.
 */
class ValveStageBuild : public ValveStage
{
  public:
    /**
     * @brief Builder class for ValveStage construction.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = VALVE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the valve stage.
         * @return Reference to this builder for chaining.
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the queue size (optional).
         * @param size Queue size.
         * @return Reference to this builder for chaining.
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Set whether the queue is leaky (optional).
         * @param activate True to enable leaky mode.
         * @return Reference to this builder for chaining.
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Set whether to enable tracing (optional).
         * @param activate True to enable tracing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build and return the ValveStage.
         * @return Shared pointer to the constructed ValveStage.
         */
        std::shared_ptr<ValveStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for ValveStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
