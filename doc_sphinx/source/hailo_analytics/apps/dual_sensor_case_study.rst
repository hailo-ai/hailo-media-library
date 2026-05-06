============================================
Dual Sensor Processing Reference Application
============================================

Overview
========
This example shows a dual sensor vision pipeline where each sensor produces a single stream.
The key takeaway from this example is how to manage multiple sensors simultaneously, accessing video feeds from two sensors 
via the Media Library and encoding those video streams using separate encoders for streaming.

This application demonstrates how to:
- Configure and manage two sensors independently
- Handle separate media library configurations for each sensor
- Create distinct pipelines for each sensor while avoiding port conflicts
- Stream multiple video feeds simultaneously

.. image:: /_images_src/hailo_analytics/apps/dual_sensor_single_stream/dual_sensor_single_stream_app.png
    :alt: Application Simple
    :align: center

Prerequisites
=============

Before running the application, you need to configure the device tree overlay:

.. code-block:: bash

    $ fw_setenv dtb_overlays '#conf-hailo_hailo15-sbc-sensor-1.dtbo'
    $ reboot

After the reboot, the system will be ready to run the dual sensor application.


Running the Application
=======================

The Hailo-15 Vision Processor Software package includes the Dual Sensor application pre-compiled and ready to run.

To run this application, follow these steps:

1. On the host machine, set up and run the Analytic Draw Client to receive and display video feeds from both sensors.
   
   First, ensure you have completed the environment setup as described in the :doc:`analytic_viewer` documentation.
   
   Since this application handles two sensors, it will be necessary to run separate instances of the client in different terminals:

   **Terminal 1 (Sensor 0):**
    
   .. code-block:: bash
    
       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py --port 5000 --analytic-data-ip 10.0.0.1

   **Terminal 2 (Sensor 1):**
    
   .. code-block:: bash
    
       $ cd tools/analytic_viewer/
       $ python app_analytic_draw_client.py --port 5100 --analytic-data-ip 10.0.0.1 --analytic-data-port 7001
    
   Note that Sensor 1 uses port 5100 (5000 + 100 offset) to avoid conflicts with Sensor 0.

2. On the Hailo-15H platform, run the executable located at the following path:

    .. code-block:: bash

        $ ./apps/case_studies/dual_sensor_single_stream/dual_sensor_single_stream_case_study

It will now be possible to see feeds from both sensors with inference overlays on the host machine.

Application at a Glance
=======================
So how is this dual sensor application actually built? The classes from the Hailo AI Analytics API are used to build the dual sensor pipeline here:

.. image:: /_images_src/hailo_analytics/apps/dual_sensor_single_stream/dual_sensor_single_stream_example.png
    :alt: Application Pipeline
    :align: center

The dual sensor media pipeline is built using discrete components called stages, where each stage performs a specific task such as encoding or adding overlays. 
Each stage runs in its own thread, allowing the pipeline to process data from both sensors efficiently in parallel, stay responsive under load, and remain modular and easy to extend.

Unlike the single sensor case study, this application manages **two independent sensor pipelines** within a single application framework.
Each sensor has its own:
- Media Library configuration
- Frontend Stage
- Encoder Stage(s)
- UDP Stage(s)

To house and manage the different stages used, a single **Pipeline** instance is created that coordinates both sensor pipelines. This class manages all enclosed stages from both sensors and allows
the user to *start* and *stop* streaming for the entire system simultaneously.

.. image:: /_images_src/hailo_analytics/apps/stages/pipeline_class.png
    :alt: pipeline class
    :align: center

Key Architectural Differences for Dual Sensor
==============================================

**Resource Management:**

- Two separate Media Library instances (one per sensor)
- Separate configuration files for each sensor:

  - ``/etc/imaging/cfg/medialib_configs/case_studies/dual_sensor_single_stream_medialib_config_sensor_0.json``
  - ``/etc/imaging/cfg/medialib_configs/case_studies/dual_sensor_single_stream_medialib_config_sensor_1.json``

**Naming Conventions:**
- Frontend stages: ``frontend_stage_sensor_0``, ``frontend_stage_sensor_1``
- Encoder stages: ``enc_sensor_0_[stream_id]``, ``enc_sensor_1_[stream_id]``
- UDP stages: ``udp_sensor_0_[stream_id]``, ``udp_sensor_1_[stream_id]``

**Port Management:**
- Sensor 0: Uses base ports (5000+)
- Sensor 1: Uses offset ports (5100+) to avoid conflicts
- Port calculation: ``base_port + stream_offset + (sensor_index * 100)``

**Command Line Arguments:**

- ``--config-file-path-sensor-0``: Configuration path for sensor 0
- ``--config-file-path-sensor-1``: Configuration path for sensor 1
- ``--profile-sensor-0``: Profile name for sensor 0
- ``--profile-sensor-1``: Profile name for sensor 1
- ``--host-ip``: Target IP address for UDP streams from both sensors

Let's look at the different stages used in each sensor pipeline:

1. **Frontend Stage (Per Sensor)**: Each sensor has its own frontend stage responsible for accessing the video feed from that specific sensor via the Media Library. Each frontend captures frames from its respective camera and passes them to the next stage in its pipeline.
   
   Besides capturing frames, each Frontend also performs sensor-specific image adjustments, such as **dewarping** and **resizing**. These operations are performed on the **DSP** for hardware acceleration.

   .. image:: /_images_src/hailo_analytics/apps/stages/frontend_stage.png
       :alt: frontend stage
       :align: center

   Hardware components involved: **ISP**, **DSP**

2. **Encoder Stage (Per Sensor Per Stream)**: Each sensor has its own encoder stage(s) to encode raw video feed into an encoded format (H264/H265). This allows both sensor streams to be encoded simultaneously and independently.
   
   .. image:: /_images_src/hailo_analytics/apps/stages/encoder_stage.png
       :alt: encoder stage
       :align: center

   Hardware components involved: **Encoder**
   
   The Hailo-15's on-chip hardware encoder can handle multiple streams simultaneously, allowing for real-time encoding of both sensor video streams.
   This design ensures efficient resource usage while maintaining independent processing for each sensor.

3. **UDP Stage (Per Sensor Per Stream)**: Each sensor has its own UDP stage(s) responsible for sending the encoded video stream over the network using different UDP ports to avoid conflicts.
   Each UDP stage takes encoded video frames from its corresponding encoder stage and sends them to the specified IP address and sensor-specific port.

   .. image:: /_images_src/hailo_analytics/apps/stages/udp_stage.png
       :alt: udp stage
       :align: center

Putting it all together, what is commonly referred to as a **"Dual Sensor Vision Pipeline"** is now available: it facilitates simultaneous streaming from both cameras to the host machine with proper resource management and conflict avoidance. 