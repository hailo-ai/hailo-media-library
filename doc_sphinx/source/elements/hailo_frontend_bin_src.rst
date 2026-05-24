Hailo Frontendbinsrc
====================

Overview
--------

``hailofrontendbinsrc`` is responsible for advanced image correction, digital and optical zoom, stream duplication and scaling, low-light enhancement and correction and future capabilities such as neural network-based image enhancement and interface control for IQ and 3A via specific APIs.


.. note:: Currently only NV12 format are supported by ``hailofrontendbinsrc``.


Relation between ``hailofrontend`` and ``hailofrontendbinsrc``
--------------------------------------------------------------

The ``hailofrontendbinsrc`` is a high-level GStreamer element designed to simplify the video processing pipeline by encapsulating a series of essential functions, including image capturing, HDR, and the logic provided by ``hailofrontend``.

Although it is technically possible to use ``hailofrontend`` or even lower-level elements like ``hailodewarp`` and ``hailomultiresize`` individually, this approach is highly discouraged. These elements, when used in isolation, lead to increased complexity and a higher likelihood of errors. Therefore, Hailo strongly recommend customers utilize ``hailofrontendbinsrc`` as it is optimized for most applications, encapsulating best practices and reducing the need for manual intervention across the pipeline.

.. image:: /_images_src/frontendbinsrc_diagram.png
    :alt: Frontendbinsrc Diagram

Parameters
^^^^^^^^^^

| The hailofrontend element requires the user to provide configuration for the expected behavior.
| Configuration determines what features are enabled/disabled and can be provided either as a JSON file or as a JSON string.

JSON Example
------------
For more explanations on the JSON parameters, see the :ref:`Video Frontend Configurations <frontend-configurations-label>`.

.. literalinclude:: ../../../hailo-media-library/media_library/examples/frontend_config.json
   :language: json

Hierarchy
---------

.. code-block::

  Object
  +----GInitiallyUnowned
        +----GstObject
              +----GstElement
                    +----GstBin
                          +----GstHailoFrontend

  Implemented Interfaces:
    GstChildProxy

  Pad Templates:
    SINK template: 'sink'
      Availability: Always
      Capabilities:
        ANY
    
    SRC template: 'src_%u'
      Availability: On request
      Capabilities:
        ANY

  Element has no clocking capabilities.
  Element has no URI handling capabilities.

  Pads:
    SINK: 'sink'
      Pad Template: 'sink'

  Element Properties:

    async-handling      : The bin will handle Asynchronous state changes
                          flags: readable, writable
                          Boolean. Default: false
    config              : Frontendbinsrc config as frontend_config_t
                          flags: readable, writable, changeable in NULL, READY, PAUSED or PLAYING state
                          Pointer.
    config-file-path    : JSON config file path to load
                          flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                          String. Default: null
    config-string       : JSON config string to load
                          flags: readable, writable, controllable, changeable in NULL, READY, PAUSED or PLAYING state
                          String. Default: null
    freeze              : Freeze the image
                          flags: readable, writable, changeable in NULL, READY, PAUSED or PLAYING state
                          Boolean. Default: false
    hailort-config      : HailoRT config as hailort_t
                          flags: readable
                          Pointer.
    hdr-config          : HDR config as hdr_config_t
                          flags: readable
                          Pointer.
    input-video-config  : video input config as input_video_config_t
                          flags: readable
                          Pointer.
    isp-config          : isp config as isp_t
                          flags: readable
                          Pointer.
    message-forward     : Forwards all children messages
                          flags: readable, writable
                          Boolean. Default: false
    name                : The name of the object
                          flags: readable, writable, 0x2000
                          String. Default: "hailofrontendbinsrc0"
    num-buffers         : Number of buffers to output before sending EOS (-1 = unlimited)
                          flags: readable, writable
                          Integer. Range: -1 - 2147483647 Default: -1
    parent              : The parent of the object
                          flags: readable, writable, 0x2000
                          Object of type "GstObject"
    privacy-mask        : Pointer to privacy mask blender
                          flags: readable
                          Pointer.

  Children:
    hailofrontendelement
    queue0
    frontendcapsfilter
    v4l2src0
