============================================
Dual Sensor Face Landmarks Application
============================================

Overview
========

This application demonstrates how to run a face landmarks AI pipeline on one sensor while simultaneously streaming simple video feeds from a second sensor.
It combines the :doc:`face_landmarks` cascaded AI pipeline pattern with the :doc:`dual_sensor_case_study` dual sensor management approach.

- **Sensor 0**: Runs the full face landmarks AI pipeline (person/face detection followed by facial landmarks detection) with analytics metadata published over ZeroMQ
- **Sensor 1**: Runs a simple streaming pipeline without AI processing, providing a direct video feed

The results from Sensor 0 can be visualized on the host machine using the :doc:`analytic_viewer` tool, which receives the video stream via UDP and metadata via ZeroMQ, displaying face detection boxes and facial landmark points.

Prerequisites
=============

Before running the application, the device tree overlay needs to be configured to enable the second sensor:

.. code-block:: bash

    $ fw_setenv dtb_overlays '#conf-hailo_hailo15-sbc-sensor-1.dtbo'
    $ reboot

After the reboot, the system will be ready to run the dual sensor application.

Running the Application
=======================

The application will come pre-compiled and ready to run on the Hailo-15 platform as part of the release image.

To run the application, follow these steps:

1. On the host machine, set up and run the Analytic Draw Client to receive and display video feeds from both sensors.

   First, ensure you have completed the environment setup as described in the :doc:`analytic_viewer` documentation.

   Since this application handles two sensors, run separate instances of the client for each sensor:

   **Terminal 1 (Sensor 0 - Face Landmarks AI Pipeline):**

   .. code-block:: bash

       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py --udp-port 5000 --analytic-data-ip 10.0.0.1

   **Terminal 2 (Sensor 1 - Simple Stream):**

   .. code-block:: bash

       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py --udp-port 5100 --analytic-data-ip 10.0.0.1 --analytic-data-port 7001

   The use of ``--analytic-data-port 7001`` is intentional: the analytic viewer defaults to subscribing on ZMQ port 7000,
   which is where Sensor 0 publishes its face landmarks metadata. Pointing Sensor 1's viewer to an unused port
   ensures Sensor 0's overlays are not drawn on Sensor 1's video feed.

   Note that Sensor 0 uses base port 5000, while Sensor 1 uses port 5100 (offset by +100) to avoid conflicts.

2. On the Hailo-15 platform, run the executable located at the following path:

    .. code-block:: bash

        $ ./apps/face_landmarks/face_landmarks_dual_sensor_app/face_landmarks_dual_sensor_app

It should now be possible to see the video feed from Sensor 0 with face detection boxes and facial landmark points on Terminal 1,
and a plain video stream from Sensor 1 on Terminal 2.

Application at a Glance
=======================

This application combines two pipeline patterns into a single application:

**Sensor 0 - Face Landmarks AI Pipeline:**

The Sensor 0 pipeline follows the same cascaded AI architecture as the :doc:`face_landmarks` application:

1. **Vision Pipeline**: Captures video frames from Sensor 0 and encodes the non-AI streams (4K and HD) for UDP streaming. The AI stream (``sink2``) is excluded from encoding and instead routed to the AI chain.

    .. figure:: /_images_src/hailo_analytics/apps/detection/vision_pipeline.png
        :alt: vision pipeline
        :align: center

    Hardware components involved: **ISP**, **DSP**, **Encoder**

2. **Tiling Detection Pipeline**: Performs person/face detection using YOLOv8n on tiled regions of the AI stream. Tiling allows running detection on high-resolution inputs by splitting them into smaller tiles, running inference on each tile, and merging the results.

    .. figure:: /_images_src/hailo_analytics/apps/detection/tiling_detection_pipeline.png
        :alt: Tiling Detection pipeline
        :align: center

    Hardware components involved: **NN-Core**, **DSP**

3. **Face Landmarks Pipeline**: Takes detected face bounding boxes from the detection stage and runs a facial landmarks model on those regions. Produces landmark points as output.

    .. figure:: /_images_src/hailo_analytics/apps/face_landmarks/face_landmarks_subpipeline.png
        :alt: Face Landmarks pipeline
        :align: center

    Hardware components involved: **NN-Core**, **DSP**

4. **Analytics Publisher Pipeline**: Packages detection results and landmark metadata and sends them to the host machine via ZeroMQ for visualization by the :doc:`analytic_viewer`.

    .. figure:: /_images_src/hailo_analytics/apps/detection/analytics_publisher_pipeline.png
        :alt: Analytics Publisher pipeline
        :align: center

    Hardware components involved: **CPU**

**Sensor 1 - Simple Vision Pipeline:**

Sensor 1 runs a simple vision pipeline that captures video, encodes it, and streams it over UDP. No AI processing is performed on this sensor.

Architecture Notes
==================

**Dual Sensor Resource Management:**

- Two separate Media Library instances are created, one for each sensor, allowing independent configuration and control
- A single Pipeline instance coordinates stages from both sensors, enabling synchronized start/stop operations

**Port Management:**

- Sensor 0: Uses base ports (5000+) for video streams and port 7000 for ZMQ metadata
- Sensor 1: Uses offset ports (5100+) to avoid conflicts
- Both base ports are configurable via command-line arguments

**Independent Configuration:**

- Each sensor has its own media library configuration file
- Each sensor can use a different profile (e.g., different dewarp settings or rotation angles)
- AI pipeline parameters (ZMQ port) only apply to Sensor 0
