==============================================
Single Stream Processing Reference Application
==============================================

Overview
========
This reference application shows a simple single stream vision application.
The key takeaway from this reference pipeline is how to access a video feed from the Media Library and encode that video
using an encoder for streaming.

.. figure:: /_images_src/hailo_analytics/apps/single_stream/single_stream_app.png
   :alt: Single Stream Application
   :align: center
   :width: 80%

Running the Reference Application
=================================

The Hailo-15 Vision Processor Software package includes the Single stream processing pipeline pre-compiled and ready to run.

To run the single stream processing pipeline, follow these steps:

1. On the host machine, set up and run the Analytic Draw Client to receive and display the video stream with overlays.
   
   First, ensure you have completed the environment setup as described in the :doc:`analytic_viewer` documentation.
   
   Then, navigate to the analytic viewer directory and run the application:
    
   .. code-block:: bash
    
       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py
    
   The single stream processing reference application will listen on UDP port 5000 (default) for the video stream and display it with inference overlays.
   
   You can customize the port and other settings using command-line arguments. For example:
   
   .. code-block:: bash
   
       $ python app_analytic_draw_client.py --udp-port 5000 --analytic-data-ip 10.0.0.1

2. On the Hailo15 platform, run the executable located at the following path:

    .. code-block:: bash

        $ ./apps/case_studies/single_stream/single_stream_case_study

You should now be able to see the video feed with the inference overlay on the host machine screen.

Reference Application at a Glance
=================================
So how is this pipeline actually built? You can see how the classes from the Hailo AI Analytics API are used to build the pipeline here:

.. figure:: /_images_src/hailo_analytics/apps/single_stream/single_stream_pipeline.png
   :alt: Application Pipeline
   :align: center

We build a media pipeline using discrete components called stages, where each stage performs a specific task—such as encoding or adding overlays. 
Each stage runs in its own thread, allowing the pipeline to process data efficiently in parallel, stay responsive under load, and remain modular and easy to extend.

As you can see, you can build a streaming pipeline using minimal components.
To house and manage the different stages used, we create a **Pipeline** instance. This class manages the enclosed stages and allows
the user to *start* and *stop* streaming.

.. figure:: /_images_src/hailo_analytics/apps/stages/pipeline_class.png
    :alt: pipeline class
    :align: center


Nested Pipelines
----------------

Note that in the single stream processing reference application includes nested pipelines. Since **Pipeline** inherits from **Stage**, we can use pipelines as stages and nest them within other pipelines. 
This method allows us to build complex applications while keeping the code modular and organized. 
Additionally, this lets us reuse prebuilt pipelines as components in other applications. 

For example, the streaming pipeline in this example was quickly built using a generator function, and can be reused in other applications that require streaming functionality.

Lets look at the different stages used in this pipeline in the order they operate:

1. **Frontend Stage**: The frontend stage is used to access the video feed from the Media Library. It is responsible for capturing frames from the camera and passing them to the next stage in the pipeline.
   
   Besides capturing frames, the Frontend also performs some basic image adjustment, such as **dewarping** and **resizing**. these operations are performed on the **DSP** for hardware acceleration.

    .. figure:: /_images_src/hailo_analytics/apps/stages/frontend_stage.png
        :alt: frontend stage
        :align: center

    Hardware components involved: **ISP**, **DSP**

2. **Encoder Stage**: The encoder stage is used to encode raw video feed into an encoded format (H264/H265). Encoding allows the video to be streamed over the network efficiently - higher-quality transmission can be transmitted at lower bandwidth because the bytes required are compressed.
   
    .. figure:: /_images_src/hailo_analytics/apps/stages/encoder_stage.png
        :alt: encoder stage
        :align: center

    Hardware components involved: **Encoder**
   
   It is important to note that the Hailo-15 includes an on-chip hardware encoder, which allows for real-time encoding of video streams.
   This also reduces the workload required for compression from the CPU, allowing for a more efficient use of resources.
   
   The Hailo Media Library provides a C++ interface to access the hardware encoder. This API further 
   provides the **EncoderStage** class, which wraps this interface so that it may be easily used in a pipeline.

3. **UDP Stage**: The last stage in this pipeline is the UDP stage. This stage is responsible for sending the encoded video stream over the network using the UDP protocol.
   The UDP stage takes the encoded video frames from the encoder stage and sends them to a specified IP address and port.

    .. figure:: /_images_src/hailo_analytics/apps/stages/udp_stage.png
        :alt: udp stage
        :align: center

Putting it all together, we now have what is commonly referred to as a **"Vision Pipeline"**: it facilitates streaming from the camera to the host machine. 

    .. figure:: /_images_src/hailo_analytics/apps/single_stream/vision_pipeline.png
            :alt: vision pipeline
            :align: center

    A vision pipeline contains a frontend stage and output streams (enocder + UDP).