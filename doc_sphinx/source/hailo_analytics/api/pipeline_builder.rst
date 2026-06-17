.. _pipeline_builder:

PipelineBuilder
===============

Overview
--------

The ``PipelineBuilder`` class provides a fluent interface for constructing pipelines with automatic validation. It simplifies pipeline creation by:

- **Supporting flexible interleaving** of stage additions and connections
- **Providing diagnostic warnings** for disconnected stages
- **Supporting method chaining** for concise pipeline definitions
- **Providing compile-time type safety** with template methods

The builder pattern separates pipeline configuration from execution, making complex pipeline construction more readable and less error-prone.

Key Concepts
------------

Builder Pattern
^^^^^^^^^^^^^^^

PipelineBuilder uses the builder pattern to incrementally construct a pipeline. This separates the construction logic from the pipeline object itself:

.. code-block:: cpp

   // Instead of manually creating and wiring a pipeline:
   auto pipeline = std::make_shared<Pipeline>("my_pipeline");
   pipeline->add_stage(stage1, StageType::GENERAL);
   pipeline->add_stage(stage2, StageType::GENERAL);
   stage1->add_subscriber(stage2);
   
   // Use the builder for cleaner, validated construction:
   PipelineBuilder builder;
   builder.add_stage(stage1)
          .add_stage(stage2);
   builder.connect("stage1", "stage2");
   auto pipeline = builder.build("my_pipeline");

Fluent API
^^^^^^^^^^

All builder methods return ``*this``, allowing method chaining for concise pipeline definitions:

.. code-block:: cpp

   builder.add_stage(frontend)
          .add_stage(inference)
          .add_stage(postprocess)
          .connect("frontend", "inference")
          .connect("inference", "postprocess")
          .build("detection_pipeline");

Flexible Construction Order
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

PipelineBuilder allows flexible interleaving of ``add_stage()`` and ``connect()`` calls. You can add all stages first and then connect them, or interleave additions and connections:

.. code-block:: cpp

   // Add all stages first, then connect
   builder.add_stage(stage1)
          .add_stage(stage2)
          .add_stage(stage3)
          .connect("stage1", "stage2")
          .connect("stage2", "stage3")
          .build("pipeline");
   
   // Or interleave additions and connections
   builder.add_stage(stage1)
          .add_stage(stage2)
          .connect("stage1", "stage2")
          .add_stage(stage3)
          .connect("stage2", "stage3")
          .build("pipeline");

Both approaches are valid and produce the same result.

API Guide
---------

PipelineBuilder Class
^^^^^^^^^^^^^^^^^^^^^

.. doxygenclass:: hailo_analytics::pipeline::PipelineBuilder
   :project: hailo_analytics
   :members:
   :undoc-members:

Usage Examples
--------------

Simple Two-Stage Pipeline
^^^^^^^^^^^^^^^^^^^^^^^^^^

Basic pipeline with inference and postprocessing:

.. code-block:: cpp

   #include "hailo_analytics/pipeline/core/pipeline_builder.hpp"
   #include "hailo_analytics/pipeline/ai/hailort_async_stage.hpp"
   #include "hailo_analytics/pipeline/postprocess/postprocess_stage.hpp"
   
   using namespace hailo_analytics::pipeline;
   namespace ai_stages = hailo_analytics::pipeline::ai;
   
   // Create stages
   auto inference_stage = ai_stages::HailortAsyncStageBuild::create()
                                  .set_stage_name("inference")
                                  .set_hef_path("/path/to/hef");
   
   auto postprocess_stage = ai_stages::PostprocessStageBuild::create()
       .set_so_path("/path/to/postprocess.so")
       .set_function_name_opt("postprocess");
   
   // Build pipeline
   PipelineBuilder builder;
   auto pipeline = builder.add_stage(inference_stage)
                          .add_stage(postprocess_stage)
                          .connect("inference", "postprocess")
                          .build("detection");

Multi-Stream Frontend Pipeline
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Pipeline with frontend producing multiple output streams:

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   
   // Create frontend stage
   sources::FrontendStageBuild::Builder frontend_builder = sources::FrontendStageBuild::create();
   frontend_builder.set_stage_name("frontend")
                   .set_queue_size_opt(1)
                   .set_leaky_opt(false);
   std::shared_ptr<sources::FrontendStage> frontend_stage = frontend_builder.buildptr();
   
   // Configure frontend with MediaLibrary
   frontend_stage->configure(media_library);
   
   // Create processing stages for different streams
   auto process0 = /* ... create stage named "process0" ... */;
   auto process1 = /* ... create stage named "process1" ... */;
   
   // Build pipeline with frontend connections
   PipelineBuilder builder;
   auto pipeline = builder.add_stage(frontend_stage, StageType::SOURCE)
                          .add_stage(process0)
                          .add_stage(process1)
                          .connect_frontend("frontend", "stream0", "process0")
                          .connect_frontend("frontend", "stream1", "process1")
                          .build("multi_stream");

Encoder and UDP Output Pipeline
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Pipeline with encoding and UDP vision output:

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   
   // Create encoder stage
   codecs::EncoderStageBuild::Builder encoder_builder = codecs::EncoderStageBuild::create();
   encoder_builder.set_stage_name("encoder")
                  .set_queue_size_opt(1)
                  .set_leaky_opt(false);
   std::shared_ptr<codecs::EncoderStage> encoder_stage = encoder_builder.buildptr();
   
   // Configure encoder with MediaLibrary and stream ID
   encoder_stage->configure(media_library, stream_id);
   
   // Create UDP sink stage
   sinks::UdpStageBuild::Builder udp_builder = sinks::UdpStageBuild::create();
   udp_builder.set_stage_name("udp")
              .set_queue_size_opt(1)
              .set_leaky_opt(false);
   std::shared_ptr<sinks::UdpStage> udp_stage = udp_builder.buildptr();
   
   // Configure UDP (host, port, encoding type)
   udp_stage->configure("192.168.1.100", "5000", EncodingType::H264);
   
   // Build pipeline
   PipelineBuilder builder;
   auto pipeline = builder.add_stage(encoder_stage, StageType::SINK)
                          .add_stage(udp_stage, StageType::SINK)
                          .connect("encoder", "udp")
                          .build("output_pipeline");

Pipeline with Overlay Stage
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Adding overlay for visualization:

.. code-block:: cpp

   using namespace hailo_analytics::pipeline;
   namespace overlay_stage = hailo_analytics::pipeline::overlay;
   
   // Create overlay stage
   overlay_stage::OverlayStageBuild::Builder overlay_builder = overlay_stage::OverlayStageBuild::create();
   overlay_builder.set_stage_name("overlay")
                  .set_queue_size(5)
                  .set_leaky_opt(false)
                  .set_skip_opt(false);
   std::shared_ptr<overlay_stage::OverlayStage> overlay_stage_ptr = overlay_builder.buildptr();
   
   // Combine with other stages
   auto postprocess_stage = /* ... create postprocess stage ... */;
   auto encoder_stage = /* ... create encoder stage ... */;
   
   PipelineBuilder builder;
   auto pipeline = builder.add_stage(postprocess_stage)
                          .add_stage(overlay_stage_ptr)
                          .add_stage(encoder_stage, StageType::SINK)
                          .connect("postprocess", "overlay")
                          .connect("overlay", "encoder")
                          .build("overlay_pipeline");

Best Practices
--------------

Use Stage Names Consistently
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Prefer using the stage's own name via the single-parameter ``add_stage()``:

.. code-block:: cpp

   // Good: Uses stage's internal name
   builder.add_stage(inference_stage)
          .connect("inference", "postprocess");  // Must match stage->get_name()

Specify Stage Types
^^^^^^^^^^^^^^^^^^^

Explicitly set ``StageType`` for SOURCE and SINK stages to ensure proper startup/shutdown ordering:

.. code-block:: cpp

   builder.add_stage(frontend, StageType::SOURCE)     // Start first
          .add_stage(processing)                      // StageType::GENERAL (default)
          .add_stage(encoder, StageType::SINK);       // Start last, stop first

Diagnostic Warnings
^^^^^^^^^^^^^^^^^^^

The builder logs diagnostic warnings for stages that are not connected to any other stage in multi-stage pipelines. This helps catch potential configuration issues:

.. code-block:: cpp

   PipelineBuilder builder;
   builder.add_stage(stage1)
          .add_stage(stage2)
          .add_stage(stage3)  // Not connected
          .connect("stage1", "stage2")
          .build("pipeline");  // Logs warning: stage3 not connected

Note: Single-stage pipelines do not require connections and will not generate warnings. Disconnected stages in multi-stage pipelines may be intentional depending on your design.

Use Chaining for Readability
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Chain all builder calls in a single expression for compact, readable pipeline definitions:

.. code-block:: cpp

   auto pipeline = PipelineBuilder()
       .add_stage(frontend, StageType::SOURCE)
       .add_stage(ai_stage)
       .add_stage(post_stage)
       .add_stage(output_stage, StageType::SINK)
       .connect("frontend", "ai_stage")
       .connect("ai_stage", "post_stage")
       .connect("post_stage", "output_stage")
       .build("my_pipeline", true);  // Enable tracing

