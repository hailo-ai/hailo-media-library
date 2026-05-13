Profile Switching
=================

Overview
--------

This reference code example demonstrates how to dynamically switch between different imaging profiles (Daylight, Lowlight, High_Dynamic_Range) at runtime using the Media Library API. Profiles allow you to configure different image quality settings optimized for various lighting conditions and use cases.

Prerequisites
-------------

* Hailo-15 device with camera sensor connected
* Valid ``medialib_config.json`` configuration file with multiple profiles defined
* Camera sensor properly calibrated for target profiles

What You Will Learn
-------------------

* How to initialize the Media Library with a configuration file
* How to switch between imaging profiles programmatically
* How to set up encoded output streams to files
* How to use the common examples utilities for pipeline management
* How to implement graceful shutdown with signal handling

Usage
-----

.. code-block:: bash

   ./profile_switching <config.json> <iterations> <profile1> [profile2] [profile3] ...

Example:

.. code-block:: bash

   ./profile_switching /etc/imaging/cfg/medialib_configs/frontend_api_example_config.json 5 Daylight Lowlight High_Dynamic_Range

This will cycle through the specified profiles, switching every 10 seconds for 5 iterations.

Expected Behavior
-----------------

The application will:

1. Initialize the media library with the provided configuration
2. Connect frontend to encoders and start the pipeline
3. Cycle through the specified profiles in order
4. Wait 10 seconds between profile switches
5. Write encoded H.264 output to ``/var/volatile/tmp/profile_switching_sink*.h264``
6. Exit gracefully after completing all iterations
