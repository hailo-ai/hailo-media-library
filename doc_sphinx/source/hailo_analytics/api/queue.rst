.. _queue-label:

=====
Queue
=====

Overview
========

The ``Queue`` class provides a thread-safe mechanism for transferring ``Buffer`` objects between different stages in the Hailo AI Analytics pipeline. 
Queues act as the connectors between pipeline stages, enabling asynchronous processing where producer stages can continue working while consumer stages process buffers at their own pace.

Key features of the Queue class:

- **Thread-safe operations**: All queue operations are protected by mutexes and condition variables
- **Size limiting**: Maximum queue depth can be configured to control memory usage
- **Two operational modes**: Blocking mode (default) and leaky mode for different flow control strategies
- **Performance monitoring**: Built-in tracing support for queue depth tracking

Key Concepts
------------

.. figure:: /_images_src/hailo_analytics/queue.png
   :alt: Queue Contents
   :align: center
   :width: 80%

Queue Ownership and Connectivity
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In the Hailo AI Analytics pipeline, queues connect stages and are owned by the subscribing (downstream) stage:

- If **StageB subscribes to StageA**, then **StageB owns the Queue** that receives buffers from StageA
- A stage can own **multiple queues** if it has multiple input sources
- The producer stage (StageA) pushes buffers to the queue
- The consumer stage (StageB) pops buffers from the queue

This ownership model ensures that each stage manages its own input buffering and can control how it receives data from upstream stages.

Blocking Mode vs. Leaky Mode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Queue supports two modes of operation to handle full queue scenarios:

**Blocking Mode (default, leaky=false)**

.. figure:: /_images_src/hailo_analytics/blocking_queue.png
   :alt: Queue Blocked
   :align: center
   :width: 80%

- When the queue reaches ``max_buffers``, the ``push()`` call **blocks** until space becomes available
- Ensures no buffers are dropped, maintaining data integrity
- Creates backpressure upstream, slowing down producers when consumers can't keep up
- Best for scenarios where every frame **must** be processed
- Also known as a *non-leaky queue* or *blocking queue*

**Leaky Mode (leaky=true)**

.. figure:: /_images_src/hailo_analytics/leaky_queue.png
   :alt: Queue Leaky
   :align: center
   :width: 80%

- When the queue reaches ``max_buffers``, the **oldest buffer is dropped** to make room for new buffers
- Always drops the **downstream** buffer (front of queue, oldest buffer)
- Prevents blocking, allowing producers to continue at their own pace
- Best for real-time scenarios where the latest data is more important than processing every frame
- Useful for live camera feeds where dropping old frames is acceptable
- Also known as a *leaky queue* or *non-blocking queue*

Thread Safety and Synchronization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Queue class uses several synchronization primitives to ensure thread-safe operation:

- **Mutex**: Protects access to the internal queue data structure
- **Condition Variables**: Efficiently signal between threads when buffers are added or removed
- **Atomic Flushing Flag**: Coordinates shutdown and reset operations

Producer threads block in ``push()`` when the queue is full (in blocking mode), and consumer threads block in ``pop()`` when the queue is empty. Condition variables ensure threads wake up immediately when the queue state changes, avoiding busy-waiting.

Typical Workflow
----------------

1. **Queue Creation**: When connecting stages, the downstream stage creates a Queue with appropriate parameters
2. **Buffer Production**: Upstream stage processes buffers and calls ``push()`` to the subscribing stage, where it is added to the queue
3. **Buffer Transfer**: Buffers wait in the queue until the downstream stage is ready
4. **Buffer Consumption**: Downstream stage calls ``pop()`` to retrieve buffers for processing
5. **Flow Control**: Blocking (backpressure) or leaky (dropping) behavior manages queue fullness
6. **Pipeline Shutdown**: ``flush()`` is called to clear the queue and signal shutdown

Queue API Guide
===============

This section describes the main methods for working with Queues in the Hailo AI Analytics pipeline.

Queue Class
-----------

.. doxygenclass:: hailo_analytics::pipeline::Queue
   :project: hailo_analytics
   :members:
   :undoc-members:


Usage Examples
==============

Creating a Queue
----------------

.. code-block:: cpp

   // Create a blocking queue (default mode)
   // Maximum of 10 buffers, blocks on push when full
   auto queue = std::make_shared<Queue>("ParentStage", "input_queue", 10, false);
   
   // Create a leaky queue
   // Maximum of 5 buffers, drops oldest buffer when full
   auto leaky_queue = std::make_shared<Queue>("RealtimeStage", "camera_input", 5, true);


Producer: Pushing Buffers
--------------------------

.. code-block:: cpp

    // Push to the queue (given queue pointer)
    // In blocking mode: this may block if queue is full
    // In leaky mode: this never blocks, may drop old buffer
    queue->push(buffer);


Consumer: Popping Buffers
--------------------------

.. code-block:: cpp

    // Pop a buffer from the queue
    // This blocks until a buffer is available or queue is flushing
    BufferPtr buffer = queue->pop();
    
    if (buffer == nullptr) {
        // Queue is flushing and empty, handle shutdown
        return;
    }


Monitoring Queue Size
---------------------

.. code-block:: cpp

   // Check current queue depth
   int current_size = queue->size();
   std::cout << "Queue " << queue->name() << " has " 
             << current_size << " buffers" << std::endl;
   
   // This can be useful for monitoring and diagnostics
   if (current_size > WARNING_THRESHOLD) {
       LOG_WARNING("Queue backing up: " + queue->name());
   }


Flushing and Resetting
-----------------------

.. code-block:: cpp

   // During shutdown
   void shutdown() {
       // Flush all queues to unblock waiting threads
       queue->flush();
       
       // All pop() calls will now return nullptr
       // All push() calls will be ignored
   }
   
   // To restart
   void restart() {
       // Reset the queue to normal operation
       queue->reset();
       
       // Queue is now ready to accept buffers again
   }


Best Practices
==============

Choosing Queue Size
-------------------

- **Small queues (1-5 buffers)**: Minimize latency, useful for real-time applications
- **Medium queues (5-15 buffers)**: Balance between latency and throughput
- **Large queues (15+ buffers)**: Maximize throughput, tolerate processing variations
- **Consider memory constraints**: each buffer may contain large image data
- It is strongly recommended to keep queue sizes to a minimum in order to
  accommodate buffer-pool sizes in the upstream producers. Consider scenarios where the
  queue fills up, can the buffer-pool still provide buffers to the producers, or are all buffers in use?
- Monitor queue depth during development to find optimal size

Choosing Queue Mode
--------------------

**Use Blocking Mode When:**

- Every frame **must** be processed (e.g., inside analytics pipeline)
- Backpressure to slow down producers is acceptable
- Downstream processing is generally fast enough to keep up

**Use Leaky Mode When:**

- Processing live camera feeds where latest frames are most important
- Downstream processing may have occasional slowdowns
- Dropping frames is acceptable to maintain real-time responsiveness
- Want to prevent one slow stage from blocking the entire pipeline

Thread Safety Considerations
-----------------------------

- Queue operations are thread-safe, no external synchronization needed
- Multiple producers can push to the same queue safely
- However, typically one producer pushes to one queue owned by one consumer

Performance Optimization
------------------------

- Avoid calling ``size()`` frequently in hot paths (it acquires a lock)
- Use ``flush()`` during shutdown to unblock all waiting threads cleanly
- Consider queue size vs. latency tradeoff: larger queues increase latency
- Leaky queues prevent pipeline stalls but may drop many frames if too aggressive

Pipeline Shutdown
-----------------

- Always call ``flush()`` on all queues during pipeline shutdown
- This ensures any threads blocked in ``pop()`` or ``push()`` are released
- After flush, ``pop()`` returns nullptr and ``push()`` is ignored
- Use ``reset()`` if you need to restart the pipeline after flushing
- This is already handled if inheriting from ``ThreadedStage``, but take
  note if you have custom implementations

Error Handling
--------------

- Check for nullptr return from ``pop()`` to detect flushing/shutdown
- In blocking mode, be aware that ``push()`` may block indefinitely if consumer stops
- Consider using timeouts or monitoring mechanisms for production systems
- Log queue depths periodically to detect pipeline bottlenecks
