Hailo Vision
============

Overview
--------

``gsthailovision`` is a GStreamer source element that creates, owns, and manages the shared ``MediaLibrary`` instance for a pipeline. It drives video capture via the MediaLibrary frontend and exposes one dynamically-requested source pad per output stream defined in the active profile.

Downstream ``gsthailoencoder`` elements automatically obtain the same ``MediaLibrary`` instance via the internal ``MediaLibInstanceRegistry``, using the pipeline name as the lookup key.

Configuration
-------------

The element accepts configuration through two mutually exclusive properties, which must be set before the element transitions to READY:

- ``config-string`` — An inline MediaLibrary JSON configuration string.
- ``config-path`` — A file path to a MediaLibrary JSON configuration file.

Only one of the two may be set. Setting a second one after the first is already set will result in an error.

For details on the JSON configuration format, see :ref:`overview-configurations-label`.

Runtime Configuration
---------------------

Profile Switching
^^^^^^^^^^^^^^^^^

Switch to a different profile at runtime:

.. code-block:: c++

   g_object_set(vision, "profile-name", "Lowlight", NULL);

Reading the Current Profile
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Read the active profile configuration. The caller owns the returned pointer and must ``delete`` it:

.. code-block:: c++

   config_profile_t *profile = nullptr;
   g_object_get(vision, "current-profile", &profile, NULL);
   // ... use profile ...
   delete profile;

Overriding Parameters
^^^^^^^^^^^^^^^^^^^^^

Apply partial parameter changes (e.g. resolution) without a full profile switch:

.. code-block:: c++

   config_profile_t modified = *profile;
   // modify fields in modified...
   g_object_set(vision, "override-profile", &modified, NULL);

For a complete usage example, see ``hailo-media-library/api/examples/gst_vision_encoder/gst_vision_encoder_example.cpp``.

Dynamic Pads
------------

Source pads are created on request. The pad name must correspond to a stream ID from the active profile's ``encoded_output_streams`` map. For example, in a pipeline string:

.. code-block:: bash

   gsthailovision name=v config-path=config.json v.sink0 ! ...

The pad named ``sink0`` will be matched to the stream ID ``sink0`` in the profile. Each stream ID can only be claimed by one pad.

Hierarchy
---------

.. code-block::

   GObject
    +----GInitiallyUnowned
          +----GstObject
                +----GstElement
                      +----GstHailoVision

Pad Templates
-------------

.. code-block::

   SRC template: '%s'
     Availability: On request
     Capabilities:
       ANY

Element Properties
------------------

.. list-table::
   :widths: 20 15 10 55
   :header-rows: 1

   * - Property
     - Type
     - Flags
     - Description
   * - ``config-string``
     - String
     - R/W, READY
     - Inline MediaLibrary JSON configuration string.
   * - ``config-path``
     - String
     - R/W, READY
     - Path to a MediaLibrary JSON configuration file.
   * - ``profile-name``
     - String
     - R/W, PLAYING
     - Profile name to switch to at runtime.
   * - ``current-profile``
     - Pointer
     - Read-only
     - Returns a newly allocated ``config_profile_t*`` of the active profile. Caller owns the pointer and must ``delete`` it.
   * - ``override-profile``
     - Pointer
     - Write-only, PLAYING
     - Pointer to ``config_profile_t`` to apply via ``set_override_parameters()`` without a full profile switch.
