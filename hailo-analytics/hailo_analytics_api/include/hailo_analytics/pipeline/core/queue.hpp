#pragma once

// General includes
#include <atomic>
#include <chrono>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <memory>

// Infra includes
#include "buffer.hpp"

namespace hailo_analytics::pipeline
{

// Forward declaration of internal QueueTracing class
class QueueTracing;

/**
 * @brief Thread-safe queue for transferring Buffers between pipeline stages.
 *
 * The Queue class provides a thread-safe mechanism for passing BufferPtr objects between
 * different stages in the analytics pipeline. It supports two modes of operation:
 *
 * - **Blocking mode** (default): When the queue is full, push() blocks until space is available
 * - **Leaky mode**: When the queue is full, the oldest buffer is dropped to make room for new buffers
 *
 * Queues connect pipeline stages, with each queue owned by the subscribing (downstream) stage.
 * For example, if StageB subscribes to StageA, then StageB owns the Queue that receives buffers
 * from StageA. A stage may own multiple queues if it has multiple input sources.
 *
 * The Queue uses condition variables for efficient blocking/signaling and includes built-in
 * tracing support for monitoring queue depth and performance.
 */
class Queue
{
  private:
    std::queue<BufferPtr> m_queue;
    size_t m_max_buffers;
    bool m_leaky;
    std::string m_name;
    std::atomic<bool> m_flushing;
    std::unique_ptr<std::condition_variable> m_condvar;
    std::shared_ptr<std::mutex> m_mutex;
    std::unique_ptr<QueueTracing> m_tracing;

  public:
    /**
     * @brief Constructs a Queue with the specified parameters.
     * @param parent_name The name of the parent stage that owns this queue
     * @param queue_name A descriptive name for this queue instance
     * @param max_buffers The maximum number of buffers this queue can hold
     * @param leaky If true, drops oldest buffers when full; if false (default), blocks on push when full
     *
     * The parent_name and queue_name are used for logging and tracing. In blocking mode,
     * push() will block when the queue is full until pop() is called. In leaky mode,
     * the oldest buffer (front of queue) is dropped to make room for new buffers.
     */
    Queue(std::string parent_name, std::string queue_name, size_t max_buffers, bool leaky = false);
    ~Queue();
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;

    /**
     * @brief Gets the name of this queue.
     * @return The queue name string
     */
    std::string name();

    /**
     * @brief Gets the current number of buffers in the queue.
     * @return The current queue size
     *
     * This method is thread-safe and provides a snapshot of the queue size at the time of the call.
     */
    int size();

    /**
     * @brief Pushes a buffer onto the queue.
     * @param buffer The BufferPtr to add to the queue
     *
     * Behavior depends on the queue mode:
     * - **Blocking mode** (leaky=false): If the queue is full, this call blocks until space is available
     * - **Leaky mode** (leaky=true): If the queue is full, the oldest buffer is dropped and the new buffer is added
     *
     * If the queue is being flushed, the buffer is discarded without blocking.
     */
    void push(BufferPtr buffer);

    /**
     * @brief Pops a buffer from the queue.
     * @return BufferPtr from the front of the queue, or nullptr if the queue is flushing and empty
     *
     * This call blocks until a buffer is available or the queue is being flushed.
     * When a buffer is popped, space is made available for new buffers in blocking mode,
     * and waiting push() calls are notified.
     */
    BufferPtr pop();

    /**
     * @brief Checks the timestamp of the next buffer in the queue.
     * @param timeout Optional timeout duration to wait for a buffer
     * @return The timestamp of the next buffer, or 0 if no buffer is available within the timeout
     *
     * This method allows peeking at the timestamp of the next buffer without removing it from the queue.
     * If a timeout is specified, it will wait up to that duration for a buffer to become available.
     */
    uint64_t check_timestamp(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Flushes all buffers from the queue and enters flushing mode.
     *
     * Removes all buffers currently in the queue and sets the flushing flag.
     * This causes any blocking pop() calls to return nullptr and prevents new
     * buffers from being added via push(). Used for pipeline shutdown or reset.
     */
    void flush();

    /**
     * @brief Resets the queue to normal operation.
     *
     * Clears the flushing flag and removes any remaining buffers.
     * After reset, the queue can accept new buffers via push().
     */
    void reset();
};

using QueuePtr = std::shared_ptr<Queue>;

} // namespace hailo_analytics::pipeline
