#pragma once

/**
 * @file app_sink_stage.hpp
 * @brief Stage that allows custom application processing of buffers.
 **/

// General includes
#include <functional>
#include <memory>
#include <string>
#include <optional>

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

#define APP_QUEUE_SIZE_DEFAULT (1)

namespace hailo_analytics::pipeline::sinks
{

/**
 * @brief Class representing an application sink stage in the pipeline.
 *
 * This stage allows applications to provide a custom processing function
 * that will be called for each buffer. Useful for integrating custom
 * application logic at the end of a pipeline.
 * Typically used as a SINK stage at the end of a pipeline.
 */
class AppSinkStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::function<void(BufferPtr)> m_process_func; ///< Custom processing function.

  public:
    /**
     * @brief Construct a new AppSinkStage object.
     *
     * @param name Name of the stage.
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     */
    AppSinkStage(std::string name, size_t queue_size = APP_QUEUE_SIZE_DEFAULT, bool leaky = false,
                 bool trace_processing_operations = true);

    /**
     * @brief Initialize the app sink stage.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the app sink stage.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the app sink stage with a custom processing function.
     *
     * @param process_func Function to call for each buffer.
     * @return AppStatus Status of the configuration.
     */
    AppStatus configure(std::function<void(BufferPtr)> process_func);

    /**
     * @brief Process a buffer by calling the custom processing function.
     *
     * @param data Buffer to process.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data) override;
};

/**
 * @brief Builder-based app sink stage for simplified construction.
 *
 * Provides a builder pattern interface for creating app sink stages with
 * configurable parameters.
 */
class AppSinkStageBuild : public AppSinkStage
{
  public:
    /**
     * @brief Builder class for AppSinkStage construction.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = APP_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;
        std::function<void(BufferPtr)> m_process_func = nullptr;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the app sink stage.
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
         * @brief Set the custom processing function.
         * @param func Function to call for each buffer.
         * @return Reference to this builder for chaining.
         */
        Builder &set_process_func(std::function<void(BufferPtr)> func);

        /**
         * @brief Build and return the AppSinkStage.
         * @return Shared pointer to the constructed AppSinkStage.
         */
        std::shared_ptr<AppSinkStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for AppSinkStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
