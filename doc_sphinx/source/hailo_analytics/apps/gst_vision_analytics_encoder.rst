================================================
GStreamer Vision Analytics Reference Application
================================================

Overview
========
This example shows how to combine GStreamer elements (``gsthailovision``, ``gsthailoencoder``) with a Hailo AI Analytics tiling detection and overlay pipeline in a single application.

The key takeaway from this example is how to bridge GStreamer-based video I/O with analytics processing — using GStreamer for camera capture and H.264 encoding, while running tiling-based object detection and overlay drawing through the Hailo AI Analytics pipeline framework.

This example runs object detection on the device, draws the detection overlays directly onto the video frames, and streams the annotated video over UDP as a standard H.264 stream. No separate metadata channel is needed — the host simply receives and displays the video with the overlays already rendered.

This example also extends the :doc:`/gst_api/gst_api` example (``gst_vision_encoder``), which uses GStreamer elements for capture and encoding but does not include any analytics processing.

Running the Application
=======================

The Hailo-15 Vision Processor Software package includes the application pre-compiled and ready to run.

To run the application, follow these steps:

1. On the Hailo15 platform, run the executable:

    .. code-block:: bash

        $ ./apps/case_studies/gst_vision_analytics_encoder/gst_vision_analytics_encoder_case_study

2. On the host machine, receive and display the UDP stream. Since the detection overlays are already drawn on the frames on the device, the host only needs to decode and display the H.264 stream:

   .. code-block:: bash

       $ gst-launch-1.0 udpsrc port=5000 address=10.0.0.2 \
           ! application/x-rtp,encoding-name=H264 \
           ! queue max-size-buffers=30 max-size-bytes=0 max-size-time=0 leaky=no \
           ! rtpjitterbuffer mode=0 \
           ! queue max-size-buffers=30 max-size-bytes=0 max-size-time=0 leaky=no \
           ! rtph264depay \
           ! queue max-size-buffers=30 max-size-bytes=0 max-size-time=0 leaky=no \
           ! h264parse ! avdec_h264 \
           ! queue max-size-buffers=30 max-size-bytes=0 max-size-time=0 leaky=downstream \
           ! videoconvert n-threads=8 \
           ! queue max-size-buffers=30 max-size-bytes=0 max-size-time=0 leaky=no \
           ! fpsdisplaysink fps-update-interval=2000 name=hailo_display text-overlay=false sync=false

You should now be able to see the video feed with detection overlays on the host machine screen.

Command-Line Options
--------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Option
     - Default
     - Description
   * - ``-t, --timeout``
     - ``60``
     - Time to run in seconds
   * - ``-c, --config-file-path``
     - ``/etc/imaging/cfg/medialib_configs/face_landmarks_medialib_config.json``
     - Media Library configuration file path
   * - ``-o, --host-ip``
     - ``10.0.0.2``
     - Host IP address for UDP output
   * - ``-s, --switch-profile``
     - *(none)*
     - Profile name to switch to mid-run
   * - ``-d, --switch-delay``
     - ``10``
     - Seconds to wait before switching profile

Application at a Glance
========================

This application is built from three cooperating pipelines that pass video frames between them:

.. code-block:: text

    ┌─ GStreamer Input Pipeline ─────────────────────────────────┐
    │  gsthailovision ──sink0──→ queue → appsink (to analytics) │
    │                  ──sink1──→ queue → fakesink               │
    └────────────────────────────────────────────────────────────┘
                              │
                              ▼
    ┌─ Analytics Pipeline ───────────────────────────────────────┐
    │  GstSourceStage → TilingDetectionPipeline → OverlayStage  │
    │                                           → GstSinkStage  │
    └────────────────────────────────────────────────────────────┘
                              │
                              ▼
    ┌─ GStreamer Output Pipeline ────────────────────────────────┐
    │  appsrc → gsthailoencoder → rtph264pay → udpsink          │
    └────────────────────────────────────────────────────────────┘

GStreamer Input Pipeline
------------------------

The input pipeline uses ``gsthailovision`` to capture video from the camera sensor. It exposes two output streams:

- **sink0** — routed to an ``appsink``, which feeds frames into the analytics pipeline for detection and overlay processing.
- **sink1** — routed to a ``fakesink`` (discarded). This secondary stream is configured in the Media Library profile but not used by the analytics path.

Analytics Pipeline
------------------

The analytics pipeline is built using the Hailo AI Analytics pipeline framework and consists of these stages:

1. **GstSourceStage** — Pulls frames from the GStreamer ``appsink`` into the analytics pipeline.
2. **TilingDetectionPipeline** — A prebuilt subpipeline that performs tiling-based object detection. It splits high-resolution frames into tiles, runs inference on each tile using the NN-Core, and merges the detection results. Hardware components involved: **DSP**, **NN-Core**.
3. **OverlayStage** — Draws detection bounding boxes and labels directly onto the video frames.
4. **GstSinkStage** — Pushes the annotated frames back into a GStreamer ``appsrc`` for encoding.

GStreamer Output Pipeline
-------------------------

The output pipeline takes the annotated frames from the ``appsrc``, encodes them to H.264 using ``gsthailoencoder``, packetizes with ``rtph264pay``, and sends the stream over UDP to the host machine.

Shared MediaLibrary Instance
-----------------------------

A critical design detail: both GStreamer pipelines (input and output) are created with the **same pipeline name** (``"vision-analytics-encoder"``). The ``gsthailovision`` and ``gsthailoencoder`` elements use the pipeline name as a key to look up and share a single ``MediaLibrary`` instance. This ensures the encoder can find the stream configuration set up by the vision element.

Despite sharing the ``MediaLibrary``, the input and output are separate ``GstPipeline`` objects to avoid deadlocks during GStreamer state transitions.

Profile Switching
-----------------

The application supports runtime profile switching via the ``--switch-profile`` and ``--switch-delay`` command-line options. After the specified delay, the ``gsthailovision`` element's ``profile-name`` property is updated, triggering a reconfiguration of the entire shared ``MediaLibrary`` pipeline.
