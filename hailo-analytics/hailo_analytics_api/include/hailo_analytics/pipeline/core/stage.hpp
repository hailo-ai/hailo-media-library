#pragma once

// General includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage_tracing.hpp"

namespace hailo_analytics::pipeline
{

/**
 * @brief Status codes returned by application operations.
 */
enum class AppStatus
{
    SUCCESS = 0,             ///< Operation completed successfully
    INVALID_ARGUMENT,        ///< Invalid argument provided
    CONFIGURATION_ERROR,     ///< Configuration error
    BUFFER_ALLOCATION_ERROR, ///< Failed to allocate buffer
    HAILORT_ERROR,           ///< HailoRT library error
    DSP_OPERATION_ERROR,     ///< DSP operation error
    UNINITIALIZED,           ///< Component not initialized
    PIPELINE_ERROR,          ///< General pipeline error
    DMA_ERROR,               ///< DMA transfer error
    MEDIA_LIBRARY_ERROR      ///< Media library error
};

/**
 * @brief Mode for handling buffer pool exhaustion in stages.
 *
 * Defines behavior when a stage's buffer pool has no available buffers.
 */
enum class StagePoolMode
{
    FAIL_ON_EMPTY_POOL = 0, ///< Return an error when pool is empty
    LEAKY,                  ///< Drop the current frame when pool is empty
    BLOCKING,               ///< Wait until a buffer becomes available
    USE_AVAILABLE_BUFFERS,  ///< Use whatever buffers are available, even if less than requested
    STAGE_POOL_MODE_MAX     ///< Maximum enum value
};

class Stage;
using StagePtr = std::shared_ptr<Stage>;

/**
 * @brief Abstract base class for all pipeline stages.
 *
 * The Stage class defines individual processing unit in the Hailo Analytics pipeline.
 * Each stage performs a specific operation on incoming data buffers, such as
 * image capture, inference, post-processing, encoding, and more.
 * Stages can be connected together to form a pipeline where buffers flow from one
 * stage to another through queues.
 *
 * Each stage has:
 * - A unique name for identification and logging
 * - Start/stop lifecycle management
 * - Subscriber management for connecting to downstream stages
 * - Queue management for receiving buffers from upstream stages
 * - Performance tracing support
 *
 */
class Stage
{
  protected:
    std::string m_stage_name;                // Name of the stage
    std::unique_ptr<StageTracing> m_tracing; // Perfetto tracing object for the stage
    bool m_trace_processing_operations;      // Whether to trace processing operations

  public:
    /**
     * @brief Constructs a Stage with the given name.
     * @param name The name of the stage for identification and logging
     * @param trace_processing_operations If true, enables performance tracing for processing operations
     */
    Stage(std::string name, bool trace_processing_operations = true);
    virtual ~Stage() = default;

    /**
     * @brief Gets the name of this stage.
     * @return The stage name string
     */
    std::string get_name() const;

    /**
     * @brief Traces the frame rate (FPS) of this stage.
     *
     * Increments the internal frame counter for performance monitoring and tracing.
     */
    void trace_fps();

    // Virtuals to override
    /**
     * @brief Starts the stage operation.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Called to begin stage processing. Implementations should initialize resources
     * and start any necessary threads or processing loops.
     */
    virtual AppStatus start() = 0;

    /**
     * @brief Stops the stage operation.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Called to end stage processing. Implementations should clean up resources
     * and stop any running threads.
     */
    virtual AppStatus stop() = 0;

    /**
     * @brief Adds a subscriber stage that will receive buffers from this stage.
     * @param subscriber The stage to subscribe to this stage's output
     * @param stream_id Optional stream identifier for multi-stream scenarios
     *
     * When a subscriber is added, this stage will push processed buffers to the
     * subscriber's input queue.
     */
    virtual void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) = 0;

    /**
     * @brief Adds an input queue for receiving buffers from a publisher stage.
     * @param publisher_name The name of the stage that will push buffers to this queue
     *
     * Creates and registers a queue for receiving buffers from the specified publisher.
     */
    virtual void add_queue(std::string publisher_name) = 0;

    /**
     * @brief Pushes a buffer to this stage from a publisher.
     * @param data The buffer to push
     * @param publisher_name The name of the publisher stage sending the buffer
     *
     * Routes the buffer to the appropriate input queue based on the publisher name.
     */
    virtual void push(BufferPtr data, std::string publisher_name) = 0;
};

/**
 * @brief Concrete stage implementation that processes buffers in a dedicated thread.
 *
 * ThreadedStage extends Stage to provide a complete implementation with thread management,
 * queue handling, and a processing loop. This is the most common base class for custom
 * stages in the analytics pipeline.
 *
 * Key features:
 * - **Dedicated thread**: Each ThreadedStage runs in its own thread with its processing loop
 * - **Input queues**: Maintains queues for receiving buffers from upstream publishers
 * - **Subscriber management**: Can forward processed buffers to multiple downstream subscribers
 * - **Configurable queues**: Queue size and leaky/blocking behavior are configurable
 * - **Lifecycle hooks**: Provides init(), deinit(), and process() virtual methods for customization
 *
 * To create a custom stage, inherit from ThreadedStage and override:
 * - init(): Initialize resources (optional, default does nothing)
 * - process(BufferPtr): Process a single buffer (main processing logic)
 * - deinit(): Clean up resources (optional, default does nothing)
 * - loop(): Custom processing loop (optional, default loop pops from queue and calls process)
 */
class ThreadedStage : public Stage
{
  protected:
    // Threading parameters
    std::atomic<bool> m_end_of_stream = false;
    std::thread m_thread;

    // Queue parameters
    size_t m_queue_size;
    bool m_leaky;
    std::vector<QueuePtr> m_queues;

    // Subscribers
    std::vector<StagePtr> m_subscribers;

  public:
    /**
     * @brief Constructs a ThreadedStage with the specified parameters.
     * @param name The name of the stage for identification and logging
     * @param queue_size Maximum number of buffers each input queue can hold
     * @param leaky If true, queues drop old buffers when full; if false, queues block when full
     * @param trace_processing_operations If true, enables performance tracing for processing operations
     *
     * The queue parameters apply to all input queues created for this stage.
     */
    ThreadedStage(std::string name, size_t queue_size, bool leaky = false, bool trace_processing_operations = true);

    // Overrides
    /**
     * @brief Starts the stage by initializing and launching the processing thread.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Calls init() for stage-specific initialization, then creates and starts the
     * processing thread running the loop() method. Sets the thread name on Linux.
     */
    AppStatus start() override;

    /**
     * @brief Stops the stage by signaling end-of-stream and joining the thread.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Sets the end-of-stream flag, waits for the thread to complete, then calls
     * deinit() for stage-specific cleanup.
     */
    AppStatus stop() override;

    /**
     * @brief Adds a subscriber and creates the connecting queue.
     * @param subscriber The stage to subscribe to this stage's output
     * @param stream_id Optional stream identifier (currently unused)
     *
     * Registers the subscriber and instructs it to create an input queue for receiving
     * buffers from this stage.
     */
    void add_subscriber(StagePtr subscriber, std::optional<std::string> stream_id = std::nullopt) override;

    /**
     * @brief Creates an input queue for receiving buffers from a publisher.
     * @param publisher_name The name of the stage that will push buffers to this queue
     *
     * Creates a new Queue with the stage's configured queue_size and leaky parameters.
     */
    void add_queue(std::string publisher_name) override;

    /**
     * @brief Pushes a buffer to this stage's input queue.
     * @param data The buffer to push
     * @param publisher_name The name of the publisher stage sending the buffer
     *
     * Finds the queue associated with the publisher_name and pushes the buffer to it.
     */
    void push(BufferPtr data, std::string publisher_name) override;

    // Virtuals to override
    /**
     * @brief Initializes the stage before processing begins.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Called once before the processing thread starts. Override to allocate resources,
     * initialize hardware, load models, etc. Default implementation does nothing.
     */
    virtual AppStatus init();

    /**
     * @brief Deinitializes the stage after processing ends.
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Called once after the processing thread stops. Override to release resources,
     * cleanup hardware, etc. Default implementation does nothing.
     */
    virtual AppStatus deinit();

    /**
     * @brief Processes a single buffer.
     * @param buffer The buffer to process
     * @return AppStatus::SUCCESS on success, error code otherwise
     *
     * Called by the default loop() for each buffer popped from the input queue.
     * Override with your stage's processing logic. Default implementation does nothing.
     */
    virtual AppStatus process(BufferPtr buffer);

    /**
     * @brief Main processing loop that runs in the stage's thread.
     *
     * Default implementation continuously pops buffers from the first input queue,
     * calls process() on each buffer, and traces performance. Override for custom
     * loop behavior (e.g., processing from multiple queues, different timing, etc.).
     *
     * The loop should check m_end_of_stream to know when to exit.
     */
    virtual void loop();

    // Threaded stage functions
    /**
     * @brief Sends a buffer to all subscribers of this stage.
     * @param data The buffer to send
     *
     * Pushes the buffer to all stages that have subscribed to this stage's output.
     * Call this after processing a buffer to forward it downstream.
     */
    void send_to_subscribers(BufferPtr data);

    /**
     * @brief Sends a buffer to a specific subscriber by name.
     * @param stage_name The name of the subscriber stage to send to
     * @param data The buffer to send
     *
     * Pushes the buffer only to the subscriber with the matching name.
     * Useful for selective routing in multi-subscriber scenarios.
     */
    void send_to_specific_subscriber(std::string stage_name, BufferPtr data);

    /**
     * @brief Sets the end-of-stream flag and manages queue flushing.
     * @param end_of_stream If true, flushes all queues; if false, resets all queues
     *
     * When set to true, signals the processing loop to exit and flushes all input
     * queues to unblock any waiting pop() calls. When set to false, resets queues
     * to allow the stage to restart.
     */
    void set_end_of_stream(bool end_of_stream);

    /**
     * @brief Get the list of subscriber stages.
     * @return Vector of subscriber stages
     */
    const std::vector<StagePtr> &get_subscribers() const
    {
        return m_subscribers;
    }
};
using ThreadedStagePtr = std::shared_ptr<ThreadedStage>;

} // namespace hailo_analytics::pipeline
