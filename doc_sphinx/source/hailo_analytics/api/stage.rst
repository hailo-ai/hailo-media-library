.. _stage-label:

=====
Stage
=====

Overview
========

The ``Stage`` class is the fundamental building block of a Hailo AI Analytics pipeline. 
Stages perform specific tasks such as image capture, inference, post-processing, cropping, encoding, or streaming.
Each Stage owns and manages a thread to process data asynchronously and in parallel to other Stages. 
This allows for the pipeline to stream buffers and reduce latency. 

Key features of stages:

- **Modular Design**: Each stage encapsulates a specific processing function
- **Connectivity**: Stages can subscribe to other stages to form processing pipelines
- **Thread-Based Processing**: Each stage typically runs in its own thread for parallel execution
- **Queue Management**: Stages own input queues for receiving buffers from upstream stages
- **Extensibility**: Custom stages can be created by inheriting from base classes
- **Performance Monitoring**: Built-in tracing support for monitoring stage performance

The Hailo AI Analytics API provides two base stage classes:

- **Stage**: Abstract base class defining the stage interface
- **ThreadedStage**: Concrete implementation with thread management and a processing loop

Key Concepts
------------

.. figure:: /_images_src/hailo_analytics/stages.png
   :alt: Queue Leaky
   :align: center
   :width: 80%

Stage Connectivity and Ownership
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When connecting stages, the **subscriber (downstream) stage owns the queue**:

- If **StageB subscribes to StageA**, then **StageB owns the Queue**
- StageA (publisher) pushes buffers to StageB's queue
- StageB (subscriber) pops buffers from its own queue
- A stage can have **multiple input queues** if it subscribes to multiple publishers
- A stage can have **multiple subscribers** to broadcast buffers to multiple downstream stages

This ownership model ensures each stage controls how it receives data.

Stage Hierarchy
~~~~~~~~~~~~~~~

.. figure:: /_images_src/hailo_analytics/stage_inheritance.png
   :alt: Stage Hierarchy
   :align: center
   :width: 80%

   Note what capabilities are inherited from the base classes.

The biggest takeaway from the hierarchy is that Pipeline is a type of Stage.
This means that all the capabilities and behaviors defined for Stage also apply to Pipeline,
and therefore Pipeline can subscribe to other Stages or Pipelines. Since Pipeline also holds
Stage instances, and Pipeline is a Stage itself, it can hold other Pipeline instances.

Stage Lifecycle
~~~~~~~~~~~~~~~~

Stages follow a defined lifecycle:

1. **Construction**: Stage is created with configuration parameters
2. **Subscription**: Stages are connected using ``add_subscriber()``
3. **Start**: ``start()`` is called to initialize resources and launch the processing thread
4. **Processing**: Stage continuously processes buffers in its thread
5. **Publishing**: Processed buffers are sent to subscribers
6. **Stop**: ``stop()`` is called to signal shutdown, join thread, and cleanup resources

.. note::

   .. centered::
      All Stage variants offered in Hailo AI Analytics come with accompanying ``Builder`` classes that provide a higher-level API for constructing and configuring stages. See the Builder class documentation for more details.


ThreadedStage Processing Loop
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The default ThreadedStage processing loop:

1. **Pop** a buffer from the first input queue (blocks until available)
2. **Process** the buffer by calling the ``process()`` method
3. **Push** the processed buffer to subscribers (optional, based on logic)
4. **Trace** performance metrics
5. **Repeat** until end-of-stream is signaled

Custom stages can override ``loop()`` for more complex processing patterns, such as:

- Processing from multiple input queues
- Batch processing multiple buffers together
- Time-based processing rather than buffer-driven
- Generating new buffers without input

Thread Safety
~~~~~~~~~~~~~

Each ThreadedStage runs in its own thread, which provides:

- **Parallel execution**: Multiple stages process simultaneously
- **Thread isolation**: Each stage's processing is independent
- **Queue synchronization**: Queues handle thread-safe buffer transfer

Stages don't need to worry about thread synchronization for queue operations, but should ensure thread safety for any shared resources they access.

Creating Custom Stages
~~~~~~~~~~~~~~~~~~~~~~~

To create a custom stage:

1. **Inherit from Stage or ThreadedStage**
2. **Override process()**: Implement your processing logic
3. **Optional: Override init()**: Initialize resources (models, hardware, etc.)
4. **Optional: Override deinit()**: Cleanup resources
5. **Optional: Override loop()**: Custom processing loop if needed

The most common pattern is to inherit from ThreadedStage, and override ``process()`` while using the default loop.
This works well for most single-input, single-output stages.

Stage API Guide
===============

This section describes the main classes, enums, and methods for working with Stages in the Hailo AI Analytics pipeline.

AppStatus Enum
--------------

.. doxygenenum:: hailo_analytics::pipeline::AppStatus
   :project: hailo_analytics


StagePoolMode Enum
------------------

.. doxygenenum:: hailo_analytics::pipeline::StagePoolMode
   :project: hailo_analytics


Stage Class
-----------

.. doxygenclass:: hailo_analytics::pipeline::Stage
   :project: hailo_analytics
   :members:
   :undoc-members:


ThreadedStage Class
-------------------

.. doxygenclass:: hailo_analytics::pipeline::ThreadedStage
   :project: hailo_analytics
   :members:
   :undoc-members:


Usage Examples
==============

Creating a Simple Custom Stage
-------------------------------

.. code-block:: cpp

   // Custom stage that processes each buffer
   class MyProcessingStage : public hailo_analytics::pipeline::ThreadedStage
   {
   public:
       MyProcessingStage(std::string name, size_t queue_size, bool leaky = false, 
                         bool trace_processing_operations = true)
           : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
       {
       }
       
       AppStatus init() override
       {
           // Initialize resources (e.g., load model)
           HAILO_ANALYTICS_LOG_INFO("Initializing {}", m_stage_name);
           return AppStatus::SUCCESS;
       }
       
       AppStatus process(BufferPtr buffer) override
       {
           // Process the buffer
           HailoROIPtr roi = buffer->get_roi();
           
           // Do some processing...
           // (e.g., run inference, add detections, etc.)
           
           // Forward to subscribers
           send_to_subscribers(buffer);
           
           return AppStatus::SUCCESS;
       }
       
       AppStatus deinit() override
       {
           // Cleanup resources
           HAILO_ANALYTICS_LOG_INFO("Deinitializing {}", m_stage_name);
           return AppStatus::SUCCESS;
       }
   };


Subscribing Stages
------------------

.. code-block:: cpp

   // Connect stages (build the pipeline graph)
   // given stages source_stage, processing_stage, and sink_stage
   source_stage->add_subscriber(processing_stage);
   processing_stage->add_subscriber(sink_stage);
   
   // Start pipeline (start in reverse order)
   sink_stage->start();
   processing_stage->start();
   source_stage->start();
   
   // Let it run...
   std::this_thread::sleep_for(std::chrono::seconds(10));
   
   // Stop pipeline (stop in forward order)
   source_stage->stop();
   processing_stage->stop();
   sink_stage->stop();


Custom Processing Loop
----------------------

.. code-block:: cpp

   // Stage with custom loop that processes from multiple queues
   class MultiInputStage : public hailo_analytics::pipeline::ThreadedStage
   {
   public:
       MultiInputStage(std::string name, size_t queue_size, bool leaky = false,
                       bool trace_processing_operations = true)
           : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
       {
       }
       
       void loop() override
       {
           while (!m_end_of_stream)
           {
               // Custom logic: alternate between queues
               if (m_queues.size() < 2)
               {
                   std::this_thread::sleep_for(std::chrono::milliseconds(10));
                   continue;
               }
               
               // Pop from first queue
               BufferPtr buffer1 = m_queues[0]->pop();
               if (buffer1 == nullptr) break;
               
               // Pop from second queue
               BufferPtr buffer2 = m_queues[1]->pop();
               if (buffer2 == nullptr) break;
               
               // Process both buffers together
               process_dual_input(buffer1, buffer2);
               
               trace_fps();
           }
       }
       
   private:
       void process_dual_input(BufferPtr buf1, BufferPtr buf2)
       {
           // Custom processing with two inputs
           // ...
       }
   };


Selective Subscriber Routing
-----------------------------

.. code-block:: cpp

   class RouterStage : public hailo_analytics::pipeline::ThreadedStage
   {
   public:
       RouterStage(std::string name, size_t queue_size, bool leaky = false,
                   bool trace_processing_operations = true)
           : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
       {
       }
       
       AppStatus process(BufferPtr buffer) override
       {
           // Route based on some condition
           HailoROIPtr roi = buffer->get_roi();
           auto detections = roi->get_objects_typed(HAILO_DETECTION);
           
           if (detections.size() > 0)
           {
               // Send to analytics stage if detections found
               send_to_specific_subscriber("AnalyticsStage", buffer);
           }
           else
           {
               // Send to bypass stage if no detections
               send_to_specific_subscriber("BypassStage", buffer);
           }
           
           return AppStatus::SUCCESS;
       }
   };


Leaky vs Blocking Queue Configuration
--------------------------------------

.. code-block:: cpp

   // Blocking stage: ensures every frame is processed
   // Queue will block producers when full
   auto blocking_stage = std::make_shared<ProcessingStage>(
       "blocking_processor",
       10,      // queue_size
       false,   // leaky = false (blocking mode)
       true     // trace_processing_operations
   );
   
   // Leaky stage: drops old frames if consumer is slow
   // Good for real-time camera feed processing
   auto leaky_stage = std::make_shared<ProcessingStage>(
       "realtime_processor",
       5,       // queue_size
       true,    // leaky = true (leaky mode)
       true     // trace_processing_operations
   );


Broadcasting to Multiple Subscribers
-------------------------------------

.. code-block:: cpp

   // Given stages source, subscriber1, subscriber2, and subscriber3
   
   // Source broadcasts to all three subscribers
   source->add_subscriber(subscriber1);
   source->add_subscriber(subscriber2);
   source->add_subscriber(subscriber3);
   
   // Inside the source stage's process() method:
   send_to_subscribers(buffer);  // Goes to all three


Error Handling in Processing
-----------------------------

.. code-block:: cpp

   class RobustStage : public hailo_analytics::pipeline::ThreadedStage
   {
   public:
       RobustStage(std::string name, size_t queue_size, bool leaky = false,
                   bool trace_processing_operations = true)
           : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
       {
       }
       
       AppStatus process(BufferPtr buffer) override
       {
           try
           {
               // Attempt processing
               auto status = do_processing(buffer);
               
               if (status != AppStatus::SUCCESS)
               {
                   HAILO_ANALYTICS_LOG_ERROR("Processing failed in {}", m_stage_name);
                   return status;
               }
               
               // Forward to subscribers
               send_to_subscribers(buffer);
               return AppStatus::SUCCESS;
               
           }
           catch (const std::exception& e)
           {
               HAILO_ANALYTICS_LOG_ERROR("Exception in {}: {}", m_stage_name, e.what());
               return AppStatus::PIPELINE_ERROR;
           }
       }
       
   private:
       AppStatus do_processing(BufferPtr buffer)
       {
           // Your processing logic
           return AppStatus::SUCCESS;
       }
   };


Best Practices
==============

Stage Design
------------

- **Single Responsibility**: Each stage should have one clear purpose
- **Error Handling**: Always handle errors gracefully and return appropriate AppStatus
- **Resource Management**: Use RAII principles, allocate in init(), cleanup in deinit()
- **Performance**: Minimize processing time in process() to maintain throughput

Queue Configuration
-------------------

- **Queue Size**: Balance between latency and memory usage
  
  - Smaller queues (1-10): Lower latency, less buffering
  - Larger queues (10-50): Better throughput, longer latency

- **Leaky Mode**: Use for real-time scenarios where dropping frames is acceptable
- **Blocking Mode**: Use when every frame must be processed

Pipeline Construction
---------------------

- **Start Order**: Start stages in reverse topological order (sinks before sources)
- **Stop Order**: Stop stages in forward topological order (sources before sinks)
- **Connection Validation**: Ensure all stages are properly connected before starting
- **Resource Cleanup**: Always call stop() to ensure proper cleanup

Threading Considerations
------------------------

- **Thread Count**: Each ThreadedStage creates one thread; consider CPU cores
- **Blocking Operations**: Avoid blocking operations in process() that could stall the pipeline
- **Thread Safety**: Ensure any shared resources accessed by stages are thread-safe
- **Thread Names**: ThreadedStage automatically sets thread names on Linux for debugging

Performance Optimization
------------------------

- **Minimize Copies**: Avoid unnecessary buffer or data copies, note we work exclusively with pointers
- **Use Metadata**: Store additional information as metadata rather than copying buffers
- **Profile Stages**: Use built-in tracing to identify bottlenecks
- **Balance Queues**: Monitor queue depths to find imbalances in the pipeline
- **Optimize Hot Paths**: Focus optimization on the most frequently called code (process())

Error Recovery
--------------

- **Graceful Degradation**: Continue processing even if one buffer fails
- **Logging**: Log errors with sufficient context for debugging
- **Status Codes**: Return appropriate AppStatus codes for different error types
- **Cleanup**: Ensure resources are released even when errors occur

Pipeline Shutdown
-----------------

- **Signal End-of-Stream**: Use set_end_of_stream() to signal graceful shutdown
- **Flush Queues**: Queues are automatically flushed when end-of-stream is set
- **Join Threads**: stop() automatically joins the processing thread
- **Cleanup Order**: Stop sources first to prevent new buffers, then stop downstream stages

Testing Custom Stages
----------------------

- **Unit Test process()**: Test the processing logic independently
- **Integration Test**: Test the stage in a minimal pipeline
- **Load Test**: Verify performance under expected load
- **Error Cases**: Test error handling and recovery
- **Resource Leaks**: Check for memory leaks with long-running tests
