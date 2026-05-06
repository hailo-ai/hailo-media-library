====================================
Dynamic Privacy Mask Case Study
====================================

Overview
========
This example shows a dynamic privacy masking pipeline that applies real-time privacy masks to detected objects directly on the Hailo-15 device.
The key takeaway from this example is how to combine AI inference (detection and segmentation) with the hardware-accelerated privacy mask blender in the encoder, producing privacy-masked video output without any host-side post-processing.

Unlike the detection and face landmarks case studies which stream metadata for host-side overlay, this application applies masking directly in the encoder on-device.
AI detection and segmentation results are stored in the Analytics Database and the encoder's privacy mask blender queries this database each frame to apply masks to the encoded video stream.

The application supports configurable object labels (person, vehicle, face, license_plate), two masking modes (solid color overlay or pixelization), and automatically adapts resource usage to the platform (Hailo-15H or Hailo-15L).

Optionally, detection metadata can also be streamed over ZeroMQ for visualization on the host machine using the :doc:`analytic_viewer` tool.

Running the Application
=======================

The application will come pre-compiled and ready to run on the Hailo15 platform as part of the release image.

To run the dynamic privacy mask application, follow these steps:

1. On the Hailo15 platform, run the executable:

    .. code-block:: bash

        $ ./apps/case_studies/dynamic_privacy_mask/dynamic_privacy_mask_app

    The application will start streaming privacy-masked video over UDP to the host machine.

2. (Optional) To visualize detection metadata on the host machine, enable ZMQ output and run the Analytic Draw Client:

   First, ensure you have completed the environment setup as described in the :doc:`analytic_viewer` documentation.

   Then run the application with a ZMQ port:

    .. code-block:: bash

        $ ./apps/case_studies/dynamic_privacy_mask/dynamic_privacy_mask_app --zmq-port 7000

   And on the host machine:

    .. code-block:: bash

        $ cd tools/analytic_viewer/
        $ python app_analytic_draw_client.py --udp-port 5000 --analytic-data-ip 10.0.0.1

Application at a Glance
========================

This application builds two main pipelines: a **Vision Pipeline** for video capture and streaming, and a **DPM AI Pipeline** for detection, segmentation, and analytics storage. The AI pipeline results feed back into the encoder's privacy mask blender, creating a closed-loop system where the output video stream already has privacy masks applied.

Lets look at the different subpipelines used in this application in the order they operate:

1. **Vision Pipeline**: The Vision Pipeline captures video frames from the sensor, encodes them, and streams over UDP. The encoder in this pipeline includes the **Privacy Mask Blender**, which queries the Analytics Database each frame to retrieve segmentation masks and apply them to the encoded output. This is where the actual masking happens — the AI pipeline produces the data, and the encoder consumes it.

    .. figure:: /_images_src/hailo_analytics/apps/detection/vision_pipeline.png
        :alt: Vision pipeline
        :align: center

    Hardware components involved: **ISP**, **DSP**, **Encoder**

2. **Tiling Detection Pipeline**: The Tiling Detection Pipeline splits high-resolution frames into smaller tiles, runs YOLOv8n object detection on each tile using the NN-Core, and merges the results. Tiling allows accurate detection on high-resolution inputs without downscaling artifacts. The DSP handles the tiling operations for hardware acceleration.

    .. figure:: /_images_src/hailo_analytics/apps/detection/tiling_detection_pipeline.png
        :alt: Tiling Detection pipeline
        :align: center

    Hardware components involved: **NN-Core**, **DSP**

3. **Lightweight Tracker**: An IOU-based tracker that maintains object identity across frames. This reduces jitter in the privacy masks by providing consistent tracking IDs, ensuring that masks follow objects smoothly rather than flickering as detections appear and disappear between frames.

    Hardware components involved: **CPU**

4. **Detection Limiter**: Filters detections by label and caps the number of detections per frame to stay within resource limits (12 on Hailo-15H, 6 on Hailo-15L). The limiter uses a label hierarchy system where enabling a parent label automatically includes its children:

    * **person** automatically includes **face**
    * **vehicle** automatically includes **license_plate**

    Hardware components involved: **CPU**

5. **DPM Segmentation Pipeline**: For each detected bounding box, the DSP crops the region from the frame, and the LinkNet model (128x128 input) runs semantic segmentation on the NN-Core to produce a per-pixel mask. An aggregator combines the individual crop results back into a unified set of segmentation masks for the frame.

    Hardware components involved: **NN-Core**, **DSP**

6. **Analytics Database**: Stores the semantic segmentation results with the ID ``"semantic_segmentation"``. The encoder's privacy mask blender queries this database every frame to retrieve the latest masks and render them onto the encoded video. This is the bridge between the AI pipeline and the encoder.

    Hardware components involved: **CPU**

7. **(Optional) Metadata Sender**: When a ZMQ port is specified, this stage publishes detection metadata over ZeroMQ. This allows the host-side :doc:`analytic_viewer` to display detection boxes alongside the privacy-masked video stream for debugging and visualization purposes.

    Hardware components involved: **CPU**

Putting it all together, we have a pipeline that captures video from the sensor, detects and segments objects of interest, stores the segmentation masks in the Analytics Database, and applies those masks in real-time directly in the encoder — producing a privacy-compliant video stream at the edge.


On-Device Privacy Masking Pattern
==================================

This application demonstrates an important architectural pattern: **On-Device Privacy Masking**.

Key Concepts:

* **Closed-Loop AI-to-Encoder**: AI inference results feed back into the video encoder via the Analytics Database, enabling privacy masking directly on the device without external processing or host-side post-processing.
* **Cascaded Detection + Segmentation**: Object detection narrows the regions of interest, and segmentation produces precise per-pixel masks only within those regions. This two-stage approach is more efficient than running full-frame segmentation.
* **Label Hierarchy**: Parent-child label relationships (person->face, vehicle->license_plate) allow coarse label selection while automatically including fine-grained sub-labels for comprehensive masking.
* **Platform-Aware Resource Management**: Detection limits automatically adapt to platform capabilities (Hailo-15H supports more concurrent detections than Hailo-15L), ensuring stable real-time performance.
* **Configurable Mask Appearance**: Supports both solid color overlay and pixelization modes, configurable at runtime through the Privacy Mask API or JSON configuration.

This pattern is commonly used in privacy-sensitive applications:

* GDPR-compliant video surveillance
* Privacy-preserving traffic monitoring
* Edge-based anonymization for video analytics

The Hailo Analytics API makes it easy to build such privacy masking pipelines by providing prebuilt generators for tiling detection, segmentation, and analytics database integration. For details on the Privacy Mask API and configuration, see :ref:`privacy-mask-label`.
