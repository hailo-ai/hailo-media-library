#pragma once

/**
 * @file udp_stage.hpp
 * @brief Stage that sends encoded video data over UDP network.
 **/

// General includes
#include <algorithm>

// Media-Library includes
#include "media_library/encoder.hpp"

// Infra includes
#include "udp_module.hpp"

#define UDP_QUEUE_SIZE_DEFAULT (1)

namespace hailo_analytics::pipeline::sinks
{

/**
 * @brief Class representing a UDP sink stage in the pipeline.
 *
 * This stage sends encoded video data over UDP to a specified host and port.
 * It receives encoded buffers from upstream stages and transmits them via UDP.
 * Typically used as a SINK stage at the end of a pipeline.
 */
class UdpStage : public hailo_analytics::pipeline::ThreadedStage
{
  private:
    std::string m_host;  ///< Target host address.
    std::string m_port;  ///< Target port number.
    EncodingType m_type; ///< Encoding type of the data.
    UdpModulePtr m_udp;  ///< UDP module for sending data.
    bool m_print_fps;    ///< Whether to print FPS information.

  public:
    /**
     * @brief Construct a new UdpStage object.
     *
     * @param name Name of the stage.
     * @param queue_size Size of the processing queue.
     * @param leaky If true, drops oldest buffer when queue is full.
     * @param trace_processing_operations If true, enables performance tracing.
     * @param print_fps If true, prints FPS information.
     */
    UdpStage(std::string name, size_t queue_size = UDP_QUEUE_SIZE_DEFAULT, bool leaky = false,
             bool trace_processing_operations = true, bool print_fps = false);

    /**
     * @brief Create the UDP stage with connection parameters.
     *
     * @param host Target host address (e.g., "192.168.1.100").
     * @param port Target port number (e.g., "5000").
     * @param type Encoding type of the data being sent.
     * @return AppStatus Status of the creation.
     */
    AppStatus create(std::string host, std::string port, EncodingType type);

    /**
     * @brief Initialize the UDP stage.
     *
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the UDP stage.
     *
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the UDP stage with connection parameters.
     *
     * @param host Target host address (e.g., "192.168.1.100").
     * @param port Target port number (e.g., "5000").
     * @param type Encoding type of the data being sent.
     * @return AppStatus Status of the configuration.
     */
    AppStatus configure(std::string host, std::string port, EncodingType type);

    /**
     * @brief Process a buffer by sending it over UDP.
     *
     * @param data Buffer containing the encoded data to send.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data);
};

/**
 * @brief Builder-based UDP stage for simplified construction.
 *
 * Provides a builder pattern interface for creating UDP stages with
 * configurable parameters.
 */
class UdpStageBuild : public UdpStage
{
  public:
    /**
     * @brief Builder class for UdpStage construction.
     */
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = UDP_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_print_fps = false;
        bool m_trace = true;

      public:
        /**
         * @brief Set the stage name.
         * @param name Name for the UDP stage.
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
         * @brief Set whether to print FPS information (optional).
         * @param activate True to enable FPS printing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_printfps_opt(bool activate);

        /**
         * @brief Set whether to enable tracing (optional).
         * @param activate True to enable tracing.
         * @return Reference to this builder for chaining.
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Build and return the UdpStage.
         * @return Shared pointer to the constructed UdpStage.
         */
        std::shared_ptr<UdpStage> buildptr() const;
    };

    /**
     * @brief Create a new Builder for UdpStage construction.
     * @return Builder instance.
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
