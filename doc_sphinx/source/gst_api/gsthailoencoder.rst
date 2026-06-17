Hailo Encoder
=============

Overview
--------

``gsthailoencoder`` is a GStreamer video encoder element that delegates encoding to the MediaLibrary backend. It is identified by its ``stream-id`` property, which must match a key in the active profile's ``encoded_output_streams`` map. Designed to be used alongside ``gsthailovision`` in the same pipeline.

MediaLibrary Instance Sharing
-----------------------------

During the READY to PAUSED transition, the encoder blocks and waits for the ``gsthailovision`` element to create and initialize the ``MediaLibrary`` instance. The pipeline name is used as the registry key. The ``stream-id`` is claimed atomically — only one ``gsthailoencoder`` element per stream ID is allowed in a given pipeline.

Output Capabilities
-------------------

The encoder supports **H.264** and **H.265 (HEVC)** output in byte-stream format. The codec is selected based on the ``codec`` field in the active profile's encoder configuration (``CODEC_TYPE_H264`` or ``CODEC_TYPE_HEVC``).

Output caps are derived automatically from the active profile's encoder configuration for the given stream ID.

Hierarchy
---------

.. code-block::

   GObject
    +----GInitiallyUnowned
          +----GstObject
                +----GstElement
                      +----GstVideoEncoder
                            +----GstHailoApiEncoder

Pad Templates
-------------

.. code-block::

   SINK template: 'sink'
     Availability: Always
     Capabilities:
       ANY

   SRC template: 'src'
     Availability: Always
     Capabilities:
       video/x-h264,
         stream-format = (string) byte-stream,
         alignment = (string) au,
         profile = (string) { base, main, high }
       video/x-h265,
         stream-format = (string) byte-stream,
         alignment = (string) au,
         profile = (string) { main, main-10 }

Element Properties
------------------

.. list-table::
   :widths: 20 15 10 55
   :header-rows: 1

   * - Property
     - Type
     - Flags
     - Description
   * - ``stream-id``
     - String
     - R/W, READY
     - Mandatory stream identifier matching a key in the profile's ``encoded_output_streams`` map.
