GStreamer API Elements
======================

Overview
--------

The GStreamer API elements provide an interface for building video capture and encoding pipelines using the MediaLibrary C++ API (``media_library.hpp``). These elements wrap the ``MediaLibrary`` class and use the profile-based configuration system.

Two elements are provided:

- **gsthailovision** — A source element that creates and owns the shared ``MediaLibrary`` instance for a pipeline. Manages video capture via the frontend and exposes one dynamically-requested source pad per output stream.
- **gsthailoencoder** — A video encoder element that obtains the shared ``MediaLibrary`` instance and delegates H.264/H.265 encoding to it, identified by ``stream-id``.

Architecture
------------

A typical pipeline places one ``gsthailovision`` element at the head and one or more ``gsthailoencoder`` elements downstream. The ``gsthailovision`` element initializes the ``MediaLibrary`` and registers it in a per-pipeline singleton registry (``MediaLibInstanceRegistry``). Each ``gsthailoencoder`` element looks up the same ``MediaLibrary`` instance using the pipeline name as the registry key. This ensures all elements in a pipeline share a single ``MediaLibrary`` instance and its lifecycle.

Each output stream is represented by a dynamically-requested source pad on ``gsthailovision`` (e.g., ``v.sink0``, ``v.sink1``), connected through a queue to a corresponding ``gsthailoencoder`` element identified by its ``stream-id`` property. The encoder output is then packetized (e.g., via ``rtph264pay`` or ``rtph265pay``) and sent to a sink such as ``udpsink`` or ``filesink``.

For a ready-to-run shell script demonstrating this pipeline layout, see ``hailo-analytics/apps/case_studies/gst_vision_encoder/gst_vision_encoder_pipeline.sh``.

Reference Code Examples
-----------------------

Reference code examples are available at ``hailo-media-library/api/examples/gst_vision_encoder/gst_vision_encoder_example.cpp``. It demonstrates pipeline construction, runtime profile switching, and specific parameter overrides.

For an advanced example that combines GStreamer elements with the Hailo AI Analytics tiling detection and overlay application, see the ``gst_vision_analytics_encoder`` reference pipeline at ``hailo-analytics/apps/case_studies/gst_vision_analytics_encoder/``. It demonstrates how to bridge GStreamer-based video I/O with analytics processing in a single application.

.. toctree::
   :maxdepth: 2
   :caption: Elements

   gsthailovision
   gsthailoencoder
