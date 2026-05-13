========================================
Getting Started with Hailo Media Library
========================================

The Hailo-15™ is a family of AI Vision Processor Units (VPU) implemented as System-on-Chip (SoC) solutions, purpose-built for next-generation smart cameras. It combines Hailo’s patented, field-proven AI inference architecture with advanced computer vision pipelines, delivering superior image quality and enabling real-time, high-performance video analytics. To expose and control these capabilities, Hailo provides the Hailo Media Library — a dedicated software package that interfaces directly with the Hailo-15™ vision subsystem, offering unified and fine-grained control over video capture, video encoding, on-screen display (OSD), and image correction and enhancement. This enables developers to dynamically configure camera behavior and optimize performance across diverse applications. 

Prerequisites
=============

Before starting, ensure the following are available:

- Hailo-15™ SBC (or another board featuring the Hailo-15™) flashed with the latest Hailo-15™ software
- A host machine with an internet connection
- ``Git`` and ``GStreamer`` installed on the host machine

Hailo Media Library Package Content 
====================================

This software package includes the following components:

* Media Library – for interfacing with Hailo-15 resources from C++ applications 
* GStreamer plugin - for interfacing with Hailo-15 resources 
* Additional utilities and configurations files 
* AI vision and AI Analytics reference code examples. Refer to sections 1.5 and 9
* Reference pipelines. Refer to sections 10
* Reference applications. Refer to section 10 

Hailo Media Library Overview
============================

.. note:: Media Library is only supported on Hailo-15.

The Hailo Media Library provides unified media control and handling for the Hailo-15™ VPU, exposing an integrated API that manages the Vision subsystem, including video capture, video encoding, on-screen display (OSD), and image correction and enhancement features. It serves as an extension to native media interfaces, allowing developers to continue using v4l2, HailoRT, and GStreamer, while providing a simpler, consolidated interface that centralizes control of the Hailo-15™ vision capabilities in one place. 

In addition, the Media Library includes analytics APIs that simplify the use of HailoRT for AI inference and vision-based analytics.

The Hailo Media Library includes two components: Hailo Media Library Frontend (“hailofrontend”) and Media Library Encoder-OSD (“hailoencodebin”). 

.. image:: /_images_src/updated_hml.png

Media Library Frontend (hailofrontend)
--------------------------------------

Provides advanced video preprocessing and image enhancement through a developer-friendly C++ API:

* Advanced image correction: Lens shading, distortion correction, defog, and other image enhancements.
* Digital and optical zoom: Precise zoom control for one or multiple streams.
* Stream duplication and scaling: Flexible stream routing and resizing.
* Low-light enhancement: AI-assisted denoising and exposure optimization.
* Neural network-based image enhancement (future): AI-driven IQ improvements.
* 3A and IQ control: managing auto-exposure, auto-focus, auto-white balance, and other image quality parameters.

.. warning::
    When using Media Library Front-end, Multi-process is not supported


Media Library Encoder & OSD (hailoencodebin)
--------------------------------------------

Handles video encoding and on-screen display (OSD) overlays via an integrated C++ API:

* Stream encoding: Single or multiple streams, configurable resolution and bit depth.
* Supported formats: H.264 (AVC), H.265 (HEVC), JPEG.
* Dynamic OSD overlays: Add text, images, timestamps, or graphics in real time.
* Integration with hailofrontend: Works seamlessly with AI-enhanced streams for encoding and display.


Hailo Media Library APIs
========================

The Hailo Media Library is available in both GStreamer API and C++ API, offering the same functionality and control in each. This flexibility allows customers to choose the API that best fits their existing infrastructure and development preferences.

GStreamer API
-------------

GStreamer is a powerful multimedia framework used to build complex media processing pipelines. It supports a wide range of multimedia formats and operations, including audio and video processing, streaming, and more. It is suitable for customers who:

- Are already familiar with or have existing projects using GStreamer.
- Prefer to leverage the multimedia capabilities and extensive plugin ecosystem provided by GStreamer.
- Wish to integrate Hailo's AI vision processing capabilities seamlessly into their existing GStreamer-based media applications.


C++ API
-------

The C++ API provides direct access to the Hailo Media Library, allowing developers to integrate Hailo's AI vision processing capabilities into their applications. It is suitable for customers who:

- Are experienced with C++ and prefer to use it for their application development.
- Have existing C++ codebases and wish to integrate Hailo's functionality.
- Require the performance and control provided by a compiled language like C++.

Choosing the Right API
----------------------

Both APIs offer the same level of control and functionality, so the decision should be based on which API aligns better with your team's expertise and your project's architecture.

When deciding between the GStreamer API and the C++ API, consider the following factors:

- **Existing Infrastructure:** If your project or organization already utilizes GStreamer, choosing the GStreamer API can streamline integration and development.

- **Development Preference:** If your development team is more comfortable with C++, the C++ API might be the more natural choice.

- **Community and Plugins:** GStreamer has a wide community and is considered one of the standards for media pipelines, offering a vast array of pre-existing plugins that can enhance and simplify your development process.

AI Vision Reference Code
========================

Hailo AI Vision is comprehensive C++ and GStreamer reference implementations for smart cameras based on Hailo-15 devices. They combine vision-pipeline elements with pre-trained AI tasks to demonstrate best practices for leveraging Hailo-15 hardware-acceleration, achieving low latency, efficient memory utilization, and robust real-time performance.

These examples serve as reference code for developing end-to-end AI vision solutions on Hailo-15. They provide practical reference designs for integrating the Hailo-15 software stack into complete video and AI pipelines, supporting evaluation, demos, and application development 

AI Vision Reference Code C++ API 
--------------------------------

These reference code examples demonstrate how to use the Hailo Media Library via the C++ API with the following capabilities: 

* Integration with the Media Library vision frontend 
* Integration with hardware-accelerated encoders (H.264, H.265) 
* Computer Vision tasks such as Image cropping and resizing (tiling) via DSP acceleration 
* On-screen display (OSD) overlays using hardware-accelerated DSP 
* DMA buffer utilization for efficient memory management 
* Video streaming to remote servers via RTP 
* High Dynamic Range (HDR) processing 
* AI-ISP - Advanced 12-bit AI-based video denoising for enhanced image quality in extremely low-light conditions 

Refer to the sub-sections below for detailed descriptions. These examples are located at:

.. code-block:: sh

    /home/root/app/examples


These reference code examples focus on demonstrating core Media Library API features and functionality, providing practical blueprints for common use cases such as dynamic configuration management, on-screen display overlays, and runtime profile switching. They serve as starting points for developers learning to work with the Media Library's C++ API.

Each reference code example: 

* Demonstrates a specific feature or use case 
* Includes complete, working source code 
* Provides clear prerequisites and setup instructions 
* Explains expected behavior and output 
* Uses the common utilities framework for consistency

.. toctree::
   :maxdepth: 1

   ../api_examples/profile_switching
   ../api_examples/osd


Refer to sections 9 and 10 for additional reference code, reference pipelines and reference applications.

Refer to the Hailo-15 Vision Processor Software Package Release Notes for detailed information. 

AI Vision Reference Code Examples Using GStreamer API 
-----------------------------------------------------

Reference code examples for the use of Hailo Media Library via GStreamer API elements for building video capture and encoding pipelines, are embedded in the  :doc:`../gst_api/gst_api` section. These examples are located at ``hailo-analytics/apps/case_studies/gst_vision_encoder/``.

Supported Image Sensors
=======================

The Hailo Media Library supports various image sensors to enable flexibility across different camera applications and use cases. 

For a complete list of approved CMOS sensors for Hailo-15, refer to the Hailo-15 Approved Vendor List document. You may also review the sensor registry directory at: 

.. code-block:: sh

    hailo-media-library/media_library/src/isp/sensor_registry

.. note:: Throughout this User Guide, the IMX678 sensor is referenced in most examples and demonstrations. However, the Media Library functionality described is applicable to all supported sensors, with configuration adjustments as needed for specific sensor characteristics.