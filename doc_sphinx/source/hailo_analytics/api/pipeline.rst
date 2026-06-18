.. _pipeline-label:

========
Pipeline
========

Overview
========

.. raw:: html

    <div align="center">

.. epigraph::

    “A **Pipeline** holds **Stages** connected by **Queues** that process **Buffers**”

.. raw:: html

    </div>

The ``Pipeline`` class is a container and manager for organizing multiple stages into a cohesive processing graph. 
It provides lifecycle management for collections of stages, ensuring proper startup and shutdown ordering, and enables treating a group of stages as a single logical unit.

Key features of the Pipeline class:

- **Stage Organization**: Groups stages by type (source, general, sink) for proper ordering
- **Lifecycle Management**: Automatically starts and stops stages in the correct order
- **Composite Pattern**: Pipelines can act as stages, allowing nested pipeline architectures
- **Stage Lookup**: Find stages by name for runtime configuration and inspection
- **Input/Output Designation**: Specify which stages serve as pipeline entry and exit points
- **Connection Management**: Connect pipelines to other stages or pipelines

The Pipeline class inherits from ``Stage``, which means:

- Pipelines can be connected to other stages using ``add_subscriber()``
- Pipelines can be nested within other pipelines
- Complex multi-pipeline systems can be built hierarchically

Key Concepts
------------

.. figure:: /_images_src/hailo_analytics/pipeline.png
   :alt: Simple Pipeline
   :align: center
   :width: 80%

   A simple single stream pipeline.

Stage Types and Ordering
~~~~~~~~~~~~~~~~~~~~~~~~

Stages within a pipeline are categorized into three types:

**SOURCE Stages**
  - Generate or capture data (e.g., camera capture, file reader, network receiver)
  - Started **last** during pipeline startup
  - Stopped **first** during pipeline shutdown
  - This prevents data flow before downstream stages are ready

**GENERAL Stages**
  - Process data in the middle of the pipeline (e.g., inference, post-processing, tracking)
  - Started **middle** during pipeline startup
  - Stopped **middle** during pipeline shutdown

**SINK Stages**
  - Consume or output data (e.g., UDP, analytics exporter, file writer)
  - Started **first** during pipeline startup
  - Stopped **last** during pipeline shutdown
  - This ensures they're ready to receive data when sources start

Pipeline Directions: Upstream / Downstream
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. figure:: /_images_src/hailo_analytics/pipeline_directions.png
   :alt: Pipeline Directions
   :align: center
   :width: 80%

It is common to refer to pipelines like a flowing stream of water: with data flowing from **upstream** to **downstream**.

Startup Order
~~~~~~~~~~~~~

When ``start()`` is called on a pipeline::

    1. SINK stages start    (downstream consumers ready)
    2. GENERAL stages start (middle processing ready)
    3. SOURCE stages start  (upstream producers begin)

This ensures that each stage is ready to receive data before the upstream stage begins sending it.

Shutdown Order
~~~~~~~~~~~~~~

When ``stop()`` is called on a pipeline::

    1. SOURCE stages stop   (stop producing new data)
    2. GENERAL stages stop  (finish processing existing data)
    3. SINK stages stop     (finish consuming remaining data)

This ensures graceful shutdown where data flows cleanly to completion before stages terminate.

Input and Output Stages
~~~~~~~~~~~~~~~~~~~~~~~~

Pipelines can designate specific stages as entry and exit points:

- **Input Stage**: The stage that receives buffers from external sources
  
  - Set using ``set_in_stage()``
  - When other stages/pipelines push to this pipeline, buffers go to the input stage
  - Typically a stage near the beginning of the pipeline

- **Output Stage**: The stage that sends buffers to external sinks
  
  - Set using ``set_out_stage()``
  - When this pipeline has subscribers, buffers are pushed from the output stage
  - Typically a stage near the end of the pipeline

This abstraction allows pipelines to be connected together without exposing internal structure.

Pipelines as Stages (Sub-Pipelines)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Because Pipeline inherits from Stage, pipelines can be used anywhere a stage can be used:

- Connect a pipeline to a stage: ``stage->add_subscriber(pipeline)``
- Connect a stage to a pipeline: ``pipeline->add_subscriber(stage)``
- Connect pipelines together: ``pipeline1->add_subscriber(pipeline2)``
- Nest pipelines within pipelines: ``outer_pipeline->add_stage(inner_pipeline)``

This enables hierarchical composition of complex processing graphs.

Stage Lookup
~~~~~~~~~~~~

Pipelines maintain references to all contained stages and provide lookup by name:

- ``get_stage_by_name()`` returns a specific stage
- Useful for runtime configuration, inspection, or control
- Enables dynamic pipeline behavior based on stage state

Typical Workflow
----------------

1. **Pipeline Creation**: Create a Pipeline instance with a name
2. **Add Stages**: Add stages to the pipeline with appropriate types (SOURCE, GENERAL, SINK)
3. **Connect Stages**: Connect stages to each other using ``add_subscriber()``
4. **Set I/O Stages**: Designate input and output stages if the pipeline will be connected externally
5. **Start Pipeline**: Call ``start()`` to begin processing in correct order
6. **Processing**: Buffers flow through the pipeline stages
7. **Stop Pipeline**: Call ``stop()`` to gracefully shutdown in correct order

.. note::

   .. centered::
      Steps 1-3 in the above workflow can be simplified using the ``PipelineBuilder`` class, which provides a higher-level API for constructing pipelines. See the PipelineBuilder documentation for more details.

Pipeline API Guide
==================

This section describes the main classes, enums, and methods for working with Pipelines in the Hailo AI Analytics system.

StageType Enum
--------------

.. doxygenenum:: hailo_analytics::pipeline::StageType
   :project: hailo_analytics


Pipeline Class
--------------

.. doxygenclass:: hailo_analytics::pipeline::Pipeline
   :project: hailo_analytics
   :members:
   :undoc-members:


Usage Examples
==============

.. note::
   In practice, pipelines are typically built using the **PipelineBuilder** class, which provides
   a more convenient and robust API for pipeline construction. The examples here demonstrate the
   underlying Pipeline class API for educational purposes. See the PipelineBuilder documentation
   for the recommended approach to building pipelines.

Creating a Basic Pipeline
--------------------------

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   
   // Create a pipeline
   auto pipeline = std::make_shared<Pipeline>("my_pipeline");
   
   // Create stages
   auto frontend_stage = std::make_shared<sources::FrontendStage>("frontend", 10);
   auto inference_stage = std::make_shared<ai::HailortAsyncStage>("inference", 10);
   auto postprocess_stage = std::make_shared<ai::PostprocessStage>("postprocess", 5);

   // Configure stages as needed ...

   // Add stages to pipeline with appropriate types
   pipeline->add_stage(frontend_stage, StageType::SOURCE);
   pipeline->add_stage(inference_stage, StageType::GENERAL);
   pipeline->add_stage(postprocess_stage, StageType::SINK);

   // Connect stages together
   frontend_stage->add_subscriber(inference_stage);
   inference_stage->add_subscriber(postprocess_stage);

   // Start and stop
   pipeline->start();  // Starts: frontend → inference → postprocess
   // ... processing ...
   pipeline->stop();   // Stops: postprocess → inference → frontend


Setting Input and Output Stages
--------------------------------

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   
   // Create a pipeline that can be connected to other pipelines
   auto processing_pipeline = std::make_shared<Pipeline>("processor");
   
   // Create internal stages
   auto postprocess_stage = std::make_shared<ai::PostprocessStage>("postprocess", 10);
   auto overlay_stage = std::make_shared<overlay::OverlayStage>("overlay", 10);
   auto encoder_stage = std::make_shared<codecs::EncoderStage>("encoder", 10);
   
   // Configure stages as needed ...

   // Add and connect stages
   processing_pipeline->add_stage(postprocess_stage, StageType::GENERAL);
   processing_pipeline->add_stage(overlay_stage, StageType::GENERAL);
   processing_pipeline->add_stage(encoder_stage, StageType::SINK);
   
   postprocess_stage->add_subscriber(overlay_stage);
   overlay_stage->add_subscriber(encoder_stage);
   
   // Designate input and output for external connections
   processing_pipeline->set_in_stage(postprocess_stage);
   processing_pipeline->set_out_stage(encoder_stage);
   
   // Now the pipeline can be connected like a stage
   // External stages push to postprocess_stage
   // External subscribers receive from encoder_stage


Connecting Pipelines Together
------------------------------

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   
   // Create two pipelines
   auto capture_pipeline = std::make_shared<Pipeline>("capture");
   auto analytics_pipeline = std::make_shared<Pipeline>("analytics");
   
   // Setup capture pipeline
   auto frontend_stage = std::make_shared<sources::FrontendStage>("frontend", 10);
   capture_pipeline->add_stage(frontend_stage, StageType::SOURCE);
   capture_pipeline->set_out_stage(frontend_stage);

   // Setup analytics pipeline
   auto inference_stage = std::make_shared<ai::HailortAsyncStage>("inference", 10);
   auto postprocess_stage = std::make_shared<ai::PostprocessStage>("postprocess", 10);
   analytics_pipeline->add_stage(inference_stage, StageType::GENERAL);
   analytics_pipeline->add_stage(postprocess_stage, StageType::GENERAL);
   inference_stage->add_subscriber(postprocess_stage);
   analytics_pipeline->set_in_stage(inference_stage);
   analytics_pipeline->set_out_stage(postprocess_stage);

   // Connect pipelines together
   capture_pipeline->add_subscriber(analytics_pipeline);
   
   // Start both pipelines
   analytics_pipeline->start();
   capture_pipeline->start();


Nested Pipelines
----------------

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   
   // Create an outer pipeline that contains inner pipelines
   auto main_pipeline = std::make_shared<Pipeline>("main");
   
   // Create sub-pipelines (treating them as compound stages)
   auto inference_pipeline = std::make_shared<Pipeline>("inference_processing");
   auto output_pipeline = std::make_shared<Pipeline>("output_processing");
   
   // ... configure internal structures of sub-pipelines ...

   // Add sub-pipelines as stages in the main pipeline
   main_pipeline->add_stage(inference_pipeline, StageType::GENERAL);
   main_pipeline->add_stage(output_pipeline, StageType::GENERAL);
   inference_pipeline->add_subscriber(output_pipeline);
   
   // Starting main pipeline starts all sub-pipelines
   main_pipeline->start();


Best Practices
==============

Stage Type Assignment
---------------------

- **Categorize** stages correctly when known to ensure proper startup/shutdown ordering
- **SOURCE**: Only stages that generate new buffers (cameras, file readers, synthetic generators)
- **SINK**: Only stages that terminate buffer flow (UDP, file writers, analytics exporters)
- **GENERAL**: All processing stages in between
- When in doubt, use GENERAL - it's safe but may not be optimal for ordering

Pipeline Lifecycle
------------------

- **Start Order**: Start pipelines from downstream to upstream (sinks to sources)
- **Stop Order**: Stop pipelines from upstream to downstream (sources to sinks)
- **Always stop pipelines**: Ensure ``stop()`` is called during Cleanup
- Since Pipeline manages stage lifecycles, avoid starting/stopping individual stages manually

Input/Output Stage Selection
-----------------------------

- **Set input/output stages** when the pipeline will be connected externally
- **Choose logical boundaries**: Input stage is where external data enters, output is where it exits
- **Not required for standalone pipelines**: Only needed when treating pipeline as a stage
- **Can be the same stage**: If appropriate for your design

Pipeline Nesting
----------------

- **Use nesting for modularity**: Group related stages into sub-pipelines
- **Consider performance**: Each pipeline adds a layer of indirection
- **Balance complexity**: Too much nesting can make debugging difficult
- **Document structure**: Clearly document the purpose of each nested pipeline

Stage Lookup
------------

- **Use descriptive names**: Make stages easy to find and identify
- **Avoid frequent lookups**: Cache stage pointers if accessed repeatedly
- **Check for nullptr**: Always verify that ``get_stage_by_name()`` returns a valid pointer
- **Use for configuration**: Stage lookup is ideal for runtime configuration and control

Error Handling
--------------

- **Check start() return**: Verify that ``start()`` returns SUCCESS
- **Handle stage failures**: If a stage fails to start, the pipeline may be in an inconsistent state
- **Stop on error**: If startup fails, call ``stop()`` to cleanup stages that did start
- **Log pipeline state**: Include pipeline name in log messages for multi-pipeline systems

Performance Considerations
--------------------------

- **Pipeline overhead**: Each pipeline adds minimal overhead for lifecycle management
- **Stage count**: More stages provide flexibility but may impact latency
- **Queue sizes**: Balance between latency and throughput when configuring stage queues
- **Nesting depth**: Limit nesting depth to 2-3 levels for maintainability

Multi-Pipeline Systems
----------------------

- **Isolate concerns**: Use separate pipelines for independent processing paths
- **Share resources carefully**: Be cautious about sharing stages between pipelines
- **Coordinate startup**: Ensure dependent pipelines start in the correct order
- **Monitor independently**: Track performance metrics per pipeline
- **Plan shutdown**: Stop pipelines in dependency order during system shutdown

Testing Pipelines
-----------------

- **Unit test stages**: Test individual stages before adding to pipeline
- **Integration test**: Test complete pipeline with all stages connected
- **Test startup/shutdown**: Verify correct ordering and no resource leaks
- **Test error cases**: Ensure pipeline handles stage failures gracefully
- **Load test**: Verify performance under expected data rates
