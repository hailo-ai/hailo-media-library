====================================
Face Landmarks Reference Application
====================================

Overview
========
This example shows a face landmarks detection pipeline that builds upon the basic detection pipeline.
The key takeaway from this example is how to chain multiple AI stages together, where the output of one AI model (person/face detection) feeds into another AI model (facial landmarks detection).
This app demonstrates a common pattern in computer vision: using detection results as input regions for more specialized analysis.

This application runs on the Hailo-15 backend and streams video along with face detection and landmark metadata over the network. 
The results can be visualized on the host machine using the :doc:`analytic_viewer` tool, which receives the video stream via UDP and metadata via ZeroMQ, displaying face detection boxes and facial landmark points.

.. figure:: /_images_src/hailo_analytics/apps/face_landmarks/face_landmarks_app.png
   :alt: Face Landmarks Application
   :align: center
   :width: 80%

Running the Application
=======================

The Hailo-15 Vision Processor Software package includes the Face Landmark application pre-compiled and ready to run.

To run the face_landmarks application, follow these steps:

1. On the host machine, set up and run the Analytic Draw Client to receive and display the video stream with overlays.
   
   First, ensure you have completed the environment setup as described in the :doc:`analytic_viewer` documentation.
   
   Then, navigate to the analytic viewer directory and run the application:
    
   .. code-block:: bash
    
       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py
    
   The application will listen on UDP port 5000 (default) for the video stream and display it with inference overlays.
   
   You can customize the port and other settings using command-line arguments. For example:
   
   .. code-block:: bash
   
       $ python app_analytic_draw_client.py --udp-port 5000 --analytic-data-ip 10.0.0.1

2. On the Hailo15 platform, run the executable located at the following path:

    .. code-block:: bash

        $ ./apps/face_landmarks/face_landmarks_app

You should now be able to see the video feed with face detection boxes and facial landmark points overlaid on the host machine screen.

Application at a Glance
=======================
You can see how the classes from the Hailo AI Analytics API are used to build the pipeline here:

.. figure:: /_images_src/hailo_analytics/apps/face_landmarks/face_landmarks_pipeline.png
    :alt: Application Pipeline
    :align: center

This application extends the detection pipeline by adding a **Face Landmarks** stage that performs additional AI inference on detected faces.
The pipeline demonstrates a cascaded AI architecture where detection results guide subsequent specialized analysis.

Lets look at the different stages used in this pipeline in the order they operate:

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

3. **Face Landmarks Pipeline**: The Face Landmarks Pipeline is a subpipeline that takes the detected face bounding boxes from the previous stage and runs a facial landmarks model on those regions. This pipeline performs cropping and preprocessing of the face regions, runs inference on the NN-Core, and produces landmark points as output.
    Cropping is handled by the DSP for hardware acceleration, and the landmarks model runs on the NN-Core. This subpipeline also comes prebuilt for easy integration.

    .. figure:: /_images_src/hailo_analytics/apps/face_landmarks/face_landmarks_subpipeline.png
        :alt: Face Landmarks pipeline
        :align: center

    Hardware components involved: **NN-Core**, **DSP**

4. **Analytics Publisher Pipeline**: The Analytics Publisher Pipeline is responsible for taking the detection results (bounding boxes, labels) and sending them to the host machine via ZeroMQ. This allows the host-side analytic viewer to draw overlays on the video stream based on the inference results.

    .. figure:: /_images_src/hailo_analytics/apps/detection/analytics_publisher_pipeline.png
        :alt: Analytics Publisher pipeline
        :align: center

    Hardware components involved: **CPU**

Putting it all together, we have a pipeline that takes video feed from the sensor, runs cascaded AI inference (detection followed by landmarks), and streams the results with metadata over the network.

Cascaded AI Pipeline Pattern
=============================

This application demonstrates an important architectural pattern: **Cascaded AI Inference**.

Key Concepts:

* **Region-based Processing**: The landmarks model only processes face regions detected by the first model, not the entire frame
* **Modularity**: Each AI stage is independent and can be swapped or extended without affecting others
* **Metadata Flow**: Detection results (bounding boxes) flow through the pipeline as metadata, guiding subsequent stages

This pattern is commonly used in many computer vision applications:

* Face detection → Facial landmarks
* Person detection → Pose estimation
* Vehicle detection → License plate recognition

The Hailo AI Analytics API makes it easy to build such cascaded pipelines by allowing AI stages to consume and produce metadata that flows alongside the video frames.
