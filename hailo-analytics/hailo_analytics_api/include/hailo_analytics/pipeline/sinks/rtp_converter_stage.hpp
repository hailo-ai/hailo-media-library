#pragma once

/**
 * @file rtp_converter_stage.hpp
 * @brief Stage that converts encoded video data to RTP packets.
 **/

// General includes
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

// Media-Library includes
#include "media_library/encoder.hpp"

// Postprocess Tools includes
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/sinks/convert_rtp_module.hpp"

#define RTP_STAGE_QUEUE_SIZE_DEFAULT 5

namespace hailo_analytics::pipeline::sinks
{

typedef std::string rtp_session_id_t;

/**
 * @brief Class representing an RTP converter stage in the pipeline.
 *
 * This stage converts encoded video data to RTP packets and sends them to a receiver.
 * It receives encoded buffers from upstream stages and converts them to RTP format.
 * Typically used as a SINK stage at the end of a pipeline.
 */
class RTPConverterStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    /**
     * @brief Abstract base class for receiving RTP packets.
     */
    class RTPReceiver
    {
      public:
        RTPReceiver() = default;
        virtual ~RTPReceiver() = default;
        virtual void on_rtp_packet(GstSample *sample, rtp_session_id_t stream_name) = 0;
        virtual rtp_session_id_t start(std::string session_name) = 0;
        virtual void stop(std::string stream_name) = 0;
    };

    /**
     * @brief Construct a new RTPConverterStage object.
     *
     * @param name Name of the stage.
     * @param receiver Shared pointer to RTPReceiver for handling RTP packets.
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     * @param session_name Name for the RTP session.
     */
    RTPConverterStage(std::string name, std::shared_ptr<RTPReceiver> receiver,
                      size_t queue_size = RTP_STAGE_QUEUE_SIZE_DEFAULT, bool leaky = false,
                      bool trace_processing_operations = true, std::string session_name = "");
    ~RTPConverterStage() = default;

    /**
     * @brief Create the RTP converter stage with encoding type.
     *
     * @param type Encoding type of the data to convert.
     * @return AppStatus Status of the creation.
     */
    AppStatus create(EncodingType type);

    /**
     * @brief Initialize the RTP converter stage.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the RTP converter stage.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the RTP converter stage with encoding type.
     *
     * @param encoding_type Encoding type of the data to convert.
     * @return AppStatus Status of the configuration.
     */
    AppStatus configure(EncodingType encoding_type);

    /**
     * @brief Process a buffer by converting it to RTP packets.
     *
     * @param data Buffer containing the encoded data to convert.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data) override;

  private:
    /**
     * @brief Worker function for processing frames and sending RTP packets.
     */
    void callback_worker();

    // Member variables
    EncodingType m_encoding_type;
    ConvertRtpModulePtr m_rtp_converter;
    std::shared_ptr<RTPReceiver> m_receiver;
    std::thread m_send_thread;
    std::atomic<bool> m_running;
    rtp_session_id_t m_session_id;
    std::string m_session_name;
};

/**
 * @brief Builder-based RTP converter stage for simplified construction.
 *
 * Provides a builder pattern interface for creating RTP converter stages with
 * configurable parameters.
 */
class RTPConverterStageBuild : public RTPConverterStage
{
  public:
    /**
     * @brief Builder class for RTPConverterStage construction.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_session_name;
        std::shared_ptr<RTPConverterStage::RTPReceiver> m_receiver = nullptr;
        EncodingType m_encoding_type;
        size_t m_queue_size = RTP_STAGE_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the RTP converter stage.
         * @return Reference to this builder for chaining.
         */
        Builder &set_stage_name(std::string name);

        /**
         * @brief Set the session name.
         * @param session_name Name for the RTP session.
         * @return Reference to this builder for chaining.
         */
        Builder &set_session_name(std::string session_name);

        /**
         * @brief Set the RTP receiver.
         * @param receiver Shared pointer to RTPReceiver.
         * @return Reference to this builder for chaining.
         */
        Builder &set_rtp_receiver(std::shared_ptr<RTPConverterStage::RTPReceiver> receiver);

        /**
         * @brief Set the encoding type.
         * @param type Encoding type.
         * @return Reference to this builder for chaining.
         */
        Builder &set_encoding_type(EncodingType type);

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
         * @brief Build and return the RTPConverterStage.
         * @return Shared pointer to the constructed RTPConverterStage.
         */
        std::unique_ptr<RTPConverterStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for RTPConverterStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
