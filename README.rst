Hailo Media Library
===================

.. |gstreamer| image:: https://img.shields.io/badge/gstreamer-1.16%20%7C%201.18%20%7C%201.20-blue
   :target: https://gstreamer.freedesktop.org/
   :alt: Gstreamer 1.20
   :width: 150
   :height: 20

.. |hailort| image:: https://img.shields.io/badge/HailoRT-4.23.0%20%7C%205.1.0-green
   :target: https://github.com/hailo-ai/hailort
   :alt: HailoRT 5.1.0
   :height: 20


.. |license| image:: https://img.shields.io/badge/License-LGPLv2.1-green
   :target: https://github.com/hailo-ai/hailo-media-library/blob/master/LICENSE
   :alt: License: LGPL v2.1
   :height: 20

.. image:: ./dodocuhailo_med_lib.png
  :height: 300
  :width: 600
  :align: center

|gstreamer| |hailort| |license|

----

Overview
--------

The Hailo Media Library provides media control and handling for the Hailo-15, offering an integrated C++ API set for managing the Hailo-15 vision subsystem. This includes video capture, video encoding, and image correction and enhancement capabilities.

The media library extends the native media interfaces, allowing continued use of v4l2, HailoRT, and GStreamer, while also providing a simpler and unified interface for common media operations in a single framework.

Highlights
----------
* Frontend C++ API - Stream video from the camera and apply image correction and enhancement features such as dewarp, low light enhancement, and resizing. 
* Encoder C++ API - Encode image buffers to H264 or H265 formats for streaming.
* DMA Buffer Pool API – Efficiently allocate and manage DMA buffers for high performance video streaming.

Running Examples
----------------
For comprehensive examples demonstrating how to use the media library, please refer to the `TAPPAS repository <https://github.com/hailo-ai/tappas/tree/master-vpu>`_. 
The repository includes working examples that showcase how to leverage hardware accelerated capabilities provided by the Media Library.

Further Reading
---------------
The Hailo-15 is supported by a rich ecosystem of tools and libraries. To fully leverage these resources, please visit the
`Hailo Developer Zone <https://hailo.ai/developer-zone/documentation/>`_.

In addition, we recommend reviewing the `Hailo-15 TAPPAS repository <https://github.com/hailo-ai/tappas/tree/master-vpu>`_, which contains examples and tutorials focused on media library usage.  
