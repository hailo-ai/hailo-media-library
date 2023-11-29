Hailo Media Library
===================

Media Library Overview
----------------------

The Media Library provides media control and handling for the Hailo-15 VPU.
It involves the controllability of the Vision subsystem, including video capturing, video encoding, and image correction/enhancement features.

The media library provides an API set for all of the above subsystems in a single library, written in C++.

The native interfaces to the media components remains available - there should not be any effect on v4l2, hailort, and GStreamer users, the media library comes as an extension to it and provides an easy-to-use programming interface in one location.


