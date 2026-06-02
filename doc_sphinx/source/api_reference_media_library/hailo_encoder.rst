.. _encoder-label:

=============
Encoder
=============

Overview
--------

HailoEncoder API enables the user to encode a video in h265/h264/MJPEG coding format, using the **Hailo-15 encoding hardware accelerator**.
It also supports  :ref:`OSD <osd-label>` (On Screen Display) to blend overlays on incoming frame.

Usage
------

The encoder is managed by the ``MediaLibrary`` unified API. After creating and initializing a ``MediaLibrary`` instance,
the encoder is automatically configured from the profile and started as part of the pipeline.

.. code-block:: c++

   // Create and initialize MediaLibrary (encoders are configured from the profile)
   auto media_lib_expected = MediaLibrary::create();
   auto media_library = media_lib_expected.value();
   media_library->initialize(config_string);

   // Start the pipeline (starts both frontend and all encoders)
   media_library->start_pipeline();

To add a buffer to a specific encoder stream:

.. code-block:: c++

   media_library->add_buffer_to_encoder(stream_id, buffer);

encoder_config_t can hold 2 types of configurations: hailo_encoder_config_t for h264/h265 and jpeg_encoder_config_t for mjpeg.

Examples for reading and updating a configuration of type hailo_encoder_config_t:
Update the user encoder configuration while the pipeline is running

.. code-block:: c++

      encoder_config_t encoder_config = encoder->get_user_config();
      hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
      std::cout << " Current bitrate: " << hailo_encoder_config.rate_control.bitrate.target_bitrate << std::endl;
      hailo_encoder_config.rate_control.bitrate.target_bitrate = 20000000;
      if (encoder->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
      {
         std::cout << "Failed to configure Encoder " << std::endl;
      }

Read the actual Encoder configuration (after a preset is set according to the user config) while the pipeline is running

.. code-block:: c++

      encoder_config_t encoder_config = encoder->get_config();
      hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
      std::cout << " Current gop_length: " << hailo_encoder_config.rate_control.gop_length << std::endl;

In this example we receive the actual encoder_config struct from the encoder get_config() method,
and read the gop_length value that was either set by the system or overriden by a value from the user.

Examples for reading and updating a configuration of type hailo_encoder_config_t:
Read and update the user encoder configuration while the pipeline is running:

.. code-block:: c++

      encoder_config_t encoder_config = encoder->get_user_config();
      jpeg_encoder_config_t &jpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoder_config);
      std::cout << " Current quality: " << jpeg_encoder_config.quality << std::endl;
      jpeg_encoder_config.quality = 80;
      if (encoder->configure(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
      {
        std::cout << "Failed to configure Encoder " << std::endl;
      }

Note that no preset is set for the MJPEG encoder, so the user configuration is the actual configuration.


Stop the pipeline (stops all encoders and frontend):

.. code-block:: c++

   media_library->stop_pipeline();


API
---

.. doxygentypedef:: AppWrapperCallback
   :project: media_library

.. doxygenclass:: MediaLibraryEncoder
   :project: media_library
   :members:

.. doxygentypedef:: MediaLibraryEncoderPtr
   :project: media_library

Encoder Configurations
----------------------

Encoder parameters are passed as a JSON object. Some parameters are optional — when omitted,
they fall back to a preset configuration selected automatically from resolution, codec, bitrate,
and ``rc_mode`` (h264/h265 only). The presets table lives at
``/etc/medialib/encoder_presets.csv``.

For the full per-field reference (description, value ranges, required vs optional, and a JSON
example for each block), see :ref:`overview-configurations-label`. Per-level resolution and
bitrate limits are documented in :ref:`h264_hevc_levels_table`.