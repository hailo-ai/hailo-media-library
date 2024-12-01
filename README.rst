Hailo Media Library
===================

Media Library Overview
----------------------

The Media Library provides media control and handling for the Hailo-15 VPU.
It enables controllability of the Vision subsystem, including video capturing, video encoding, and image correction/enhancement features.

The media library provides an API set for all of the above subsystems in a single library, written in C++.

The native interfaces to the media components remain available - there is no effect on v4l2, HailoRT, and GStreamer users. 
The media library comes as an extension to these and provides an easy-to-use programming interface in one location.

Highlights
----------
* Frontend CPP API - Stream video from the camera and apply image correction/enhancement features (dewarp, low light enhancement, resize, etc...). 
* Encoder CPP API - Encode image buffers to H264 or H265 for streaming.
* DMA Bufferpool API - Quickly allocate and manage DMA buffers for video streaming.

Running Examples
----------------
For comprehensive examples on how to use the media library, please refer to the `TAPPAS repository <https://github.com/hailo-ai/tappas/tree/master-vpu>`_. 
There you will find working examples that show how to leverage hardware accelerated features offered by the media library.

Further Reading
---------------
Hailo-15 comes with a rich ecosystem of tools and libraries. To make the most of those resources, you can explore the
`Hailo Developer Zone <https://hailo.ai/developer-zone/documentation/>`_.

If you have not already done so, we recommend checking out the `Hailo-15 TAPPAS repository <https://github.com/hailo-ai/tappas/tree/master-vpu>`_, which contains examples and tutorials on how to use the media library.  