Hailo Encoder
=============

Overview
--------

| hailoencodebin is a bin element that enables the user to encode a video in H265/H264/JPEG coding format, using the **Hailo-15 encoding hardware accelerator**.
| It also supports  :ref:`OSD <osd-label>` (On Screen Display) to blend overlays on incoming frame.


.. note:: JPEG Encoding is not hardware accelerated.

Relation between ``hailoencodebin``, ``hailoencoder`` and ``hailoosd``
-----------------------------------------------------------------------

The ``hailoencodebin`` is a GStreamer element designed to handle the final stages of video processing by encapsulating both the OSD and encoding functions. 

The ``hailoencodebin`` includes an OSD component, which overlays information directly onto the video stream, making it visible during playback. Following the OSD, the hailoencodebin handles the encoding of the video into formats such as H.264, H.265, or JPEG. 

While it is technically possible to use the lower-level elements ``hailoencoder`` and ``hailoosd`` individually, this approach is not recommended. By bundling these functionalities into a single element, ``hailoencodebin`` simplifies the pipeline and ensures efficient processing.

.. image:: /_images_src/encodebin-diagram.png
  :alt: Frontendbinsrc Diagram
  :width: 745
  :height: 255
  :align: center

Parameters
^^^^^^^^^^

| The hailoencodebin element provides a variety of properties that control the encoding performance and quality.
| The element requires the user to provide configuration for the expected behavior.
| Configuration determines what features are enabled/disabled and can be provided either as a JSON file or as a JSON string.

Changing Configurations at Runtime
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

| The configuration can be changed during the pipeline execution.

| Changes to such properties during the pipeline will be updated at the end of the group of images (GOP).
| A change in bitrate during the pipeline will be applied at the end of the GOP, for instance.

In the following example, the user get the encoder element and change the bitrate at runtime:

.. code-block::

  GstElementPtr encoder_element = glib_cpp::ptrs::get_bin_by_name(pipeline, "encoder");

Get the current user configuration from the encoder element:

.. code-block::

    gpointer value = nullptr;
    g_object_get(encoder_element.as_g_object(), "user-config", &value, NULL);
    encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);

Set a new target_bitrate value and update the configuration:

.. code-block::

    config->rate_control.bitrate.target_bitrate = 20000000;
    g_object_set(encoder_element.as_g_object(), "user-config", config, NULL);

Getting the actual configuration at runtime
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

| The actual running configuration (After setting defaults and values from a config preset), can be retrieved at runtime.

In the following example, the user gets the encoder element and reads the actual gop_length at runtime:

.. code-block::

  GstElementPtr encoder_element = glib_cpp::ptrs::get_bin_by_name(pipeline, "encoder");

Get the current actual configuration from the encoder element:

.. code-block::

    gpointer value = nullptr;
    g_object_get(encoder_element.as_g_object(), "config", &value, NULL);
    encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);

Read the actual gop_length value:

.. code-block::

    auto gop_length = config->rate_control.gop_length;

Hierarchy
---------

.. code-block::

      GObject
      +----GInitiallyUnowned
            +----GstObject
                  +----GstElement
                        +----GstBin
                              +----GstHailoEncodeBin

      Implemented Interfaces:
        GstChildProxy

      Pad Templates:
        SINK template: 'sink'
          Availability: Always
          Capabilities:
            ANY
        
        SRC template: 'src'
          Availability: Always
          Capabilities:
            ANY

        Element has no clocking capabilities.
        Element has no URI handling capabilities.

        Pads:
          SINK: 'sink'
            Pad Template: 'sink'
          SRC: 'src'
            Pad Template: 'src'

        Element Properties:
          async-handling      : The bin will handle Asynchronous state changes
                                flags: readable, writable
                                Boolean. Default: false
          blender             : Pointer to blender object
                                flags: readable, controllable
                                Pointer.
          config              : Pointer to the actual config object
                                flags: readable
                                Pointer.
          user-config         : Pointer to the user config object
                                flags: readable, writable, changeable in NULL, READY, PAUSED or PLAYING state
                                Pointer.
          config-file-path    : JSON config file path to load
                                flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                                String. Default: null
          config-string       : JSON config string to load
                                flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                                String. Default: null
          enforce-caps        : Enforce caps on the input/output pad of the bin
                                flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                                Boolean. Default: true
          message-forward     : Forwards all children messages
                                flags: readable, writable
                                Boolean. Default: false
          name                : The name of the object
                                flags: readable, writable, 0x2000
                                String. Default: "hailoencodebin0"
          parent              : The parent of the object
                                flags: readable, writable, 0x2000
                                Object of type "GstObject"
          queue-size          : Size of the queues in the bin, there are 2 queues.
                                flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                                Unsigned Integer. Range: 1 - 4294967295 Default: 2 
          wait-for-writable-buffer: Enables the element thread to wait until incoming buffer is writable
                                flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                                Boolean. Default: false