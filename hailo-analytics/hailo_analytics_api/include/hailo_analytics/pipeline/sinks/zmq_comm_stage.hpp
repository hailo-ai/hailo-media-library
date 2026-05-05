#pragma once

/**
 * @file zmq_comm_stage.hpp
 * @brief Stage that provides ZeroMQ-based communication for publishing and receiving messages.
 **/

// General Includes
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <numeric>
#include <iostream>

// Infra includes
#include "hailo_analytics/pipeline/core/stage.hpp"

#define QUEUE_SIZE_DEFAULT (10)

namespace hailo_analytics::pipeline::sinks
{

/**
 * @brief Stage that provides ZeroMQ (ZMQ) communication for publishing or receiving messages.
 *
 * The ZmqCommStage supports two modes of operation:
 * - PUBLISHER: Publishes buffer data as messages to a ZMQ publisher socket
 * - RECEIVER: Subscribes to and receives messages from a ZMQ subscriber socket
 *
 * This stage is useful for inter-process communication, distributed systems, or
 * integrating the analytics pipeline with external systems using ZeroMQ.
 * Typically used as a SINK stage at the end of a pipeline for publishing data,
 * or as a SOURCE stage for receiving data.
 */
class ZmqCommStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    /**
     * @brief Operating mode for the ZMQ communication stage.
     */
    enum class Mode
    {
        PUBLISHER, ///< Publish messages to a ZMQ socket
        RECEIVER   ///< Receive messages from a ZMQ socket
    };

  private:
    Mode m_mode;               ///< Operating mode (PUBLISHER or RECEIVER)
    std::string m_pub_address; ///< Publisher socket address
    std::string m_sub_address; ///< Subscriber socket address

    zmq::context_t m_context{1};               ///< ZMQ context
    zmq::socket_t m_zmq_publisher;             ///< ZMQ publisher socket
    zmq::socket_t m_zmq_subscriber;            ///< ZMQ subscriber socket
    bool m_initialized = false;                ///< Initialization state
    zmq::message_t zmq_input_msg;              ///< Input message buffer
    zmq::message_t zmq_output_msg;             ///< Output message buffer
    std::mutex m_zmq_mutex;                    ///< Mutex for ZMQ operations
    std::thread m_subscriber_thread;           ///< Thread for receiving messages
    std::atomic<bool> m_run_subscriber{false}; ///< Flag to control subscriber thread
    std::string m_latest_msg;                  ///< Latest received message
    std::mutex m_msg_mutex;                    ///< Mutex for message access
    bool m_print_fps;                          ///< Whether to print FPS statistics

    /**
     * @brief Initializes the ZMQ publisher socket.
     * @param address Address to bind the publisher socket to
     */
    void init_zmq_publisher(const std::string &address);

    /**
     * @brief Initializes the ZMQ subscriber socket.
     * @param address Address to connect the subscriber socket to
     */
    void init_zmq_subscriber(const std::string &address);

    /**
     * @brief Worker thread function for receiving messages from the subscriber socket.
     */
    void receive_messages();

  public:
    /**
     * @brief Constructs a ZmqCommStage.
     *
     * @param name The name of the stage
     * @param mode Operating mode (PUBLISHER or RECEIVER)
     * @param pub_address Publisher socket address (used in PUBLISHER mode)
     * @param sub_address Subscriber socket address (used in RECEIVER mode)
     * @param queue_size The size of the internal processing queue (default: 10)
     * @param leaky If true, drops oldest buffers when queue is full (default: false)
     * @param trace_processing_operations Enable tracing for performance analysis (default: true)
     * @param print_fps If true, prints FPS statistics (default: false)
     */
    ZmqCommStage(std::string name, Mode mode, const std::string &pub_address, const std::string &sub_address,
                 size_t queue_size = QUEUE_SIZE_DEFAULT, bool leaky = false, bool trace_processing_operations = true,
                 bool print_fps = false);

    /**
     * @brief Initializes the ZMQ communication stage.
     *
     * Sets up the appropriate ZMQ socket (publisher or subscriber) based on the mode.
     * In RECEIVER mode, also starts the subscriber thread.
     *
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitializes the ZMQ communication stage.
     *
     * Closes ZMQ sockets and stops the subscriber thread if running.
     *
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Processes a buffer by publishing it as a ZMQ message.
     *
     * In PUBLISHER mode, serializes and publishes the buffer data.
     * In RECEIVER mode, may attach received messages to the buffer.
     *
     * @param input_buffer The buffer to process
     * @return AppStatus indicating success or failure
     */
    AppStatus process(BufferPtr input_buffer) override;
};

/**
 * @brief Builder class for constructing ZmqCommStage instances.
 *
 * Provides a fluent interface for configuring and building ZmqCommStage objects
 * with various ZeroMQ communication options.
 */
class ZmqCommStageBuild
{
  public:
    /**
     * @brief Builder for ZmqCommStage configuration.
     *
     * Allows step-by-step configuration of a ZmqCommStage before building.
     */
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_trace = true;
        bool m_print_fps = false;
        ZmqCommStage::Mode m_mode = ZmqCommStage::Mode::PUBLISHER;
        std::optional<std::string> m_pub_address;
        std::optional<std::string> m_sub_address;

      public:
        /**
         * @brief Sets the stage name (required).
         * @param name The name for the ZMQ communication stage
         * @return Reference to this builder for method chaining
         */
        Builder &set_stage_name(const std::string &name);

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
        Builder &set_leaky(bool leaky);

        /**
         * @brief Enables or disables tracing for performance analysis.
         * @param activate If true, enables tracing (default: true)
         * @return Reference to this builder for method chaining
         */
        Builder &set_trace_opt(bool activate);

        /**
         * @brief Enables or disables FPS printing.
         * @param print_fps If true, prints FPS statistics (default: false)
         * @return Reference to this builder for method chaining
         */
        Builder &set_print_fps(bool print_fps);

        /**
         * @brief Sets the operating mode.
         * @param mode Operating mode (PUBLISHER or RECEIVER, default: PUBLISHER)
         * @return Reference to this builder for method chaining
         */
        Builder &set_mode(ZmqCommStage::Mode mode);

        /**
         * @brief Sets the publisher socket address.
         * @param addr Address to bind the publisher socket to (e.g., "tcp://0.0.0.0:5555")
         * @return Reference to this builder for method chaining
         */
        Builder &set_pub_address(const std::string &addr);

        /**
         * @brief Sets the subscriber socket address.
         * @param addr Address to connect the subscriber socket to (e.g., "tcp://localhost:5555")
         * @return Reference to this builder for method chaining
         */
        Builder &set_sub_address(const std::string &addr);

        /**
         * @brief Builds and returns a shared pointer to the configured ZmqCommStage.
         * @return Shared pointer to the newly created ZmqCommStage
         * @throws std::invalid_argument if required parameters are not set
         */
        std::shared_ptr<ZmqCommStage> buildptr() const;
    };

    /**
     * @brief Creates a new Builder instance for constructing a ZmqCommStage.
     * @return A new Builder instance
     */
    static Builder create();
};

} // namespace hailo_analytics::pipeline::sinks
