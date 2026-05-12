====================================================
Single Stream Object Detection Reference Application
====================================================

Overview
========
This example shows a simple detection pipeline.
The key takeaway from this example is how to run basic inference on a single stream using the Hailo-15 hardware accelerated NN-core.
This app specifically shows how to run basic inference.

This application runs on the Hailo-15 backend and streams video along with detection metadata over the network. 
The results can be visualized on the host machine using the :doc:`analytic_viewer` tool, which receives the video stream via UDP and detection metadata via ZeroMQ.

.. figure:: /_images_src/hailo_analytics/apps/detection/detection_app.png
   :alt: Detection Application
   :align: center
   :width: 80%

Running the Application
=======================

The Hailo-15 Vision Processor Software package includes the Single stream detection application pre-compiled and ready to run on the Hailo15 platform as part of the release image.

To run the application, follow these steps:

1. On the host machine, set up and run the Analytic Draw Client to receive and display the video stream with overlays.
   
   First, ensure you have completed the environment setup as described in the :doc:`analytic_viewer` documentation.
   
   Then, navigate to the analytic viewer directory and run the application:
    
   .. code-block:: bash
    
       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py
    
   The application will listen on UDP port 5000 (default) for the video stream and display it with inference overlays.
   
   You can customize the port and other settings using command-line arguments. For example:
   
   .. code-block:: bash
   
       $ python app_analytic_draw_client.py --port 5000 --analytic-data-ip 10.0.0.1

2. On the Hailo15 platform, run the executable located at the following path:

    .. code-block:: bash

        $ ./apps/case_studies/detection/detection_case_study

You should now be able to see the video feed with the inference overlay on the host machine screen.

Application at a Glance
=======================
You can see how the classes from the Hailo AI Analytics API are used to build the pipeline here:

.. figure:: /_images_src/hailo_analytics/apps/detection/detection_pipeline.png
    :alt: Application Pipeline
    :align: center

At first it may look like there are a lot of new stages compared to the :doc:`single_stream`, but we leverage **subpipelines** to manage complexity.

Lets look at the different subpipelines used in this application in the order they operate:

1. **Vision Pipeline**: The Vision Pipeline is a subpipeline that orchestrates capture of video frames from the sensor. It is responsible for interfacing with the camera hardware, configuring the video stream, and delivering raw video frames to the next stage in the pipeline. The vision pipeline typically includes stages for encoding and streaming over UDP.
    Note that in this application, the vision pipeline has two streaming outputs: 4K and HD. You can configure this behavior through the Media Library Profiles.

    .. figure:: /_images_src/hailo_analytics/apps/detection/vision_pipeline.png
        :alt: vision pipeline
        :align: center

    Hardware components involved: **ISP**, **DSP**, **NN-Core**, **Encoder**

2. **Tiling Detection Pipeline**: The Tiling Detection Pipeline is a subpipeline that performs tiling and runs the detection model on the video frames. Tiling is a technique used to process high-resolution images by splitting them into smaller tiles, running inference on each tile, and then merging the results. This allows us to run large models on high-res inputs without running into accuracy issues.
    The tiling is performed by the DSP for hardware acceleration, and the detection model runs on the NN-Core. This subpipeline comes prebuilt, allowing for easy integration into the application.

    .. figure:: /_images_src/hailo_analytics/apps/detection/tiling_detection_pipeline.png
        :alt: Tiling Detection pipeline
        :align: center

    Hardware components involved: **NN-Core**, **DSP**

3. **Analytics Publisher Pipeline**: The Analytics Publisher Pipeline is responsible for taking the detection results (bounding boxes, labels) and sending them to the host machine via ZeroMQ. This allows the host-side analytic viewer to draw overlays on the video stream based on the inference results.

    .. figure:: /_images_src/hailo_analytics/apps/detection/analytics_publisher_pipeline.png
        :alt: Analytics Publisher pipeline
        :align: center

    Hardware components involved: **CPU**

Putting it all together, we have a pipeline that takes video feed from the sensor, runs inference on it, and streams the results over the network.