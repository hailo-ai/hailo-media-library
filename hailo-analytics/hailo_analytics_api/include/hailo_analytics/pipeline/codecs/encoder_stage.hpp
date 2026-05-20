#pragma once

/**
 * @file encoder_stage.hpp
 * @brief Stage that encodes video frames using media library encoder.
 **/

// General includes
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// Media-Library includes
#include "media_library/media_library.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

#define ENCODER_QUEUE_SIZE_DEFAULT (1)

namespace hailo_analytics::pipeline::codecs
{

/**
 * @brief Class representing an encoder stage in the pipeline.
 *
 * This stage encodes video frames using the media library.
 * It receives buffers from upstream stages, encodes them, and passes
 * the encoded data to downstream stages.
 *
 * Optionally, the stage can also convert the analytics buffer's HailoROI tree
 * into typed AI metadata attached to the wrapped medialib buffer's
 * m_analytics_metadata field, delivering AI results without going through the
 * AnalyticsDB singleton.
 */
class EncoderStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    MediaLibraryInterfacePtr m_media_library; ///< Shared MediaLibrary interface instance.
    output_stream_id_t m_stream_id;           ///< Stream ID for the encoder within the MediaLibrary.

    bool m_attach_analytics_metadata = true;

    void attach_dpm_metadata(hailo_analytics::pipeline::BufferPtr data);

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
     * @brief Create the encoder stage with a MediaLibrary instance and stream ID.
     *
     * @param media_library Shared pointer to the MediaLibrary.
     * @param stream_id The output stream ID for this encoder.
     * @return AppStatus Status of the creation.
     */
    AppStatus create(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id);

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
     * @brief Configure the encoder stage with a MediaLibrary instance and stream ID.
     *
     * @param media_library Shared pointer to the MediaLibrary.
     * @param stream_id The output stream ID for this encoder.
     * @return AppStatus Status of the configuration.
     */
    AppStatus configure(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id);

    /**
     * @brief Enable the ROI -> m_analytics_metadata conversion side-effect on process().
     *
     * When enabled, process() walks the analytics buffer's HailoROI tree, builds vectors of
     * LabeledSemanticMask / LabeledDetection, and writes them onto the wrapped medialib
     * buffer's m_analytics_metadata field.
     */
    void set_attach_analytics_metadata(bool enabled);

    /**
     * @brief Process a buffer by encoding it (and optionally attaching AI metadata first).
     *
     * @param data Buffer containing the frame to encode.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data) override;
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
        bool m_attach_analytics_metadata = true;

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
         * @brief Enable the ROI -> m_analytics_metadata conversion side-effect on the built stage.
         *
         * Pixel dimensions for the wire-types come from the buffer itself at process() time.
         */
        Builder &set_attach_analytics_metadata(bool enabled = true);

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
