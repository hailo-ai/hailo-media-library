#pragma once

/**
 * @file encoder_stage.hpp
 * @brief Stage that encodes video frames using media library encoder.
 **/

// General includes
#include <algorithm>

// Media-Library includes
#include "media_library/encoder.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"

#define ENCODER_QUEUE_SIZE_DEFAULT (1)

namespace hailo_analytics::pipeline::codecs
{

/**
 * @brief Class representing an encoder stage in the pipeline.
 *
 * This stage encodes video frames using the media library encoder.
 * It receives buffers from upstream stages, encodes them, and passes
 * the encoded data to downstream stages.
 */
class EncoderStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    MediaLibraryEncoderPtr m_encoder; ///< Media library encoder instance.

  public:
    /**
     * @brief Construct a new EncoderStage object.
     *
     * @param name Name of the stage.
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     */
    EncoderStage(std::string name, size_t queue_size = ENCODER_QUEUE_SIZE_DEFAULT, bool leaky = false,
                 bool trace_processing_operations = true);

    /**
     * @brief Create the encoder stage with an encoder instance.
     *
     * @param encoder Media library encoder pointer.
     * @return AppStatus Status of the creation.
     */
    AppStatus create(MediaLibraryEncoderPtr encoder);

    /**
     * @brief Initialize the encoder stage.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the encoder stage.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the encoder stage with an encoder instance.
     *
     * @param encoder Media library encoder pointer.
     * @return AppStatus Status of the configuration.
     */
    AppStatus configure(MediaLibraryEncoderPtr encoder);

    /**
     * @brief Process a buffer by encoding it.
     *
     * @param data Buffer containing the frame to encode.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data);
};

/**
 * @brief Builder-based encoder stage for simplified construction.
 *
 * Provides a builder pattern interface for creating encoder stages with
 * configurable parameters.
 */
class EncoderStageBuild : public EncoderStage
{
  public:
    /**
     * @brief Builder class for EncoderStage construction.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = ENCODER_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the encoder stage.
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
         * @brief Build and return the EncoderStage.
         * @return Shared pointer to the constructed EncoderStage.
         */
        std::shared_ptr<EncoderStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for EncoderStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::codecs
