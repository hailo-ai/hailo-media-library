#pragma once

/**
 * @file callback_stage.hpp
 * @brief Stage that executes a user-defined callback function on the input data.
 **/

// General includes
#include <functional>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::routing
{

/**
 * @brief Stage that executes a user-defined callback function on each buffer.
 *
 * The CallbackStage allows users to inject custom processing logic into the pipeline
 * by providing a callback function that is invoked for each buffer passing through.
 * After executing the callback, the buffer is forwarded to all subscribers.
 *
 * This is useful for custom logging, debugging, metrics collection, or any other
 * user-defined processing that needs to be performed without modifying the buffer flow.
 */
class CallbackStage : public hailo_analytics::pipeline::ThreadedStage
{
  protected:
    std::function<void(BufferPtr)> m_callback; ///< User-defined callback function

  public:
    /**
     * @brief Constructs a CallbackStage.
     *
     * @param name The name of the stage
     * @param queue_size The size of the internal processing queue
     * @param leaky If true, drops oldest buffers when queue is full (default: false)
     * @param callback The callback function to execute on each buffer (default: NULL)
     * @param trace_processing_operations Enable tracing for performance analysis (default: true)
     */
    CallbackStage(std::string name, size_t queue_size, bool leaky = false,
                  std::function<void(BufferPtr)> callback = NULL, bool trace_processing_operations = true);

    /**
     * @brief Processes a buffer by executing the callback and forwarding to subscribers.
     *
     * @param data The buffer to process
     * @return AppStatus::SUCCESS on successful processing
     */
    AppStatus process(BufferPtr data) override;

    /**
     * @brief Sets or updates the callback function.
     *
     * @param callback The new callback function to execute on each buffer
     */
    void set_callback(std::function<void(BufferPtr)> callback);
};

/**
 * @brief Builder class for constructing CallbackStage instances.
 *
 * Provides a fluent interface for configuring and building CallbackStage objects.
 */
class CallbackStageBuild : public CallbackStage
{
  public:
    /**
     * @brief Builder for CallbackStage configuration.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Sets the stage name (required).
         * @param name The name for the callback stage
         * @return Reference to this builder for method chaining
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Sets the internal queue size.
         * @param size The size of the processing queue (default: 1)
         * @return Reference to this builder for method chaining
         */
        Builder &set_queue_size_opt(size_t size);

        /**
         * @brief Configures the leaky queue behavior.
         * @param activate If true, drops oldest buffers when queue is full (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_leaky_opt(bool activate);

        /**
         * @brief Enables or disables tracing for performance analysis.
         * @param activate If true, enables tracing (default: true)
         * @return Reference to this builder for method chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Builds and returns a shared pointer to the configured CallbackStage.
         * @return Shared pointer to the newly created CallbackStage
         * @throws std::invalid_argument if required parameters are not set
         */
        std::shared_ptr<CallbackStage> buildptr() const;
    };

    /**
     * @brief Creates a new Builder instance for constructing a CallbackStage.
     * @return A new Builder instance
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::routing
