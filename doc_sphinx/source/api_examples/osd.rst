OSD
===

Overview
--------

This reference code example demonstrates how to create and dynamically update on-screen display (OSD) text overlays using the Media Library API. The reference code example shows how to add text overlays to encoded video streams and update their content in real-time.

Prerequisites
-------------

* Hailo-15 device with camera sensor connected
* Valid ``medialib_config.json`` configuration file
* TrueType font file available at ``/usr/share/fonts/ttf/LiberationMono-Bold.ttf``

What You Will Learn
-------------------

* How to access the OSD blender for encoder streams
* How to create text overlays with custom positioning and styling
* How to dynamically update overlay text content at runtime
* How to enable and manage OSD overlays on multiple encoder streams
* How to use the common examples utilities for pipeline management
* How to implement graceful shutdown with signal handling

Usage
-----

.. code-block:: bash

   ./osd_example <config.json> <duration_seconds>

Example:

.. code-block:: bash

   ./osd_example /usr/bin/medialib_config.json 30

This will run the OSD example for 30 seconds, updating the overlay text every second.

Expected Behavior
-----------------

The application will:

1. Initialize the media library with the provided configuration
2. Connect frontend to encoders and start the pipeline
3. Create a centered text overlay displaying the current iteration count
4. Update the overlay text every second
5. Write encoded H.264 output with overlays to ``/var/volatile/tmp/osd_example_sink*.h264``
6. Exit gracefully after the specified duration

OSD Configuration
-----------------

The example creates a text overlay with the following properties:

* **ID**: ``status``
* **Position**: Centered horizontally at 50%, 10% from top
* **Text**: ``Iteration: N`` (updated each second)
* **Colors**: White text on black background
* **Font**: Liberation Mono Bold, 40pt
* **Alignment**: Center
