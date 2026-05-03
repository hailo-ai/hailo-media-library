.. _encoder-osd-label:

=============
Encoder
=============

Overview
--------

HailoEncoder API enables the user to encode a video in h265/h264/MJPEG coding format, using the **Hailo-15 encoding hardware accelerator**.
It also supports  :ref:`OSD <osd-label>` (On Screen Display) to blend overlays on incoming frame.

Usage
------

Create an instance of the Encoder with configuration string and stream id.

.. code-block:: c++

   tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(encoderosd_config_string, id);

Subscribe to hailofrontend callbacks: when a new buffer is available - add the buffer to the Encoder.

.. code-block:: c++

    auto streams = media_lib->frontend->get_outputs_streams();
    FrontendCallbacksMap fe_callbacks;
    frontend_output_stream_t s = streams[0];
    fe_callbacks[id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size)
    {
       encoder->add_buffer(buffer);
    };
    media_lib->frontend->subscribe(fe_callbacks);


Start the Encoder

.. code-block:: c++

   encoder->start();

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


Stop the Encoder

.. code-block:: c++

   encoder->stop();


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

Configuring the encoder's parameters is done by providing a JSON file with `config-file-path` parameter.
Some of the parameters are optional. These parameters are set according to a preset configuration, when the user does not provide them.
The preset is chosen according to resolution, codec, bitrate and rc_mode and is relevant only for h264/h265 coding formats.
The presets table is located at: /etc/medialib/encoder_presets.csv
Below is a list of the supported parameters:

.. list-table:: Stream Control 
   :widths: 20 35 15 15 15
   :header-rows: 1

   * - Name
     - Description
     - Supported
     - Can update at runtime
     - Mandatory
   * - codec
     - (H264 / H265)
     - Yes
     - Yes
     - Yes
   * - width
     - Requires encoder restart to update at runtime
     - Yes
     - No
     - Yes
   * - height
     - Requires encoder restart to update at runtime
     - Yes
     - No
     - Yes
   * - framerate
     - Requires setting `enforce-caps=false` to update at runtime 
     - Yes
     - Yes
     - Yes
   * - stream_type
     - Stream type - bytestream / plain NAL units
     - No
     - No
     - Yes
   * - bit_depth_luma
     - Luma sample bit depth of encoded bit stream (8/9/10)
     - No
     - No
     - Yes
   * - bit_depth_chroma
     - Chroma sample bit depth of encoded bit stream (8/9/10)
     - No
     - No
     - Yes
   * - format
     - Input video format
     - No
     - No
     - Yes
   * - profile
     - (base/main/high/high-10)
     - Yes
     - Yes
     - No
   * - level
     - Encoding level
     - Yes
     - Yes
     - No


.. list-table:: Gop
   :widths: 20 35 15 15 15
   :header-rows: 1

   * - Name
     - Description
     - Supported
     - Can update at runtime
     - Mandatory
   * - gop_size
     - (1-7) (1 - no B frames)
     - Yes
     - Yes
     - Yes
   * - b_frame_qp_delta
     - QP difference between BFrame QP and target QP, -1 = Disabled (1 - 51)
     - Yes
     - Yes
     - Yes

.. list-table:: Coding Control
   :widths: 20 35 15 15 15
   :header-rows: 1

   * - Name
     - Description
     - Supported
     - Can update at runtime
     - Mandatory
   * - type
     - deblock filter - enable / disable
     - No
     - No
     - Yes
   * - sei_messages
     - SEI messages configuration object with encoder_timing_sei (encoder's timing SEI) and user_metadata_sei (custom metadata SEI)
     - Yes
     - Yes
     - Yes
   * - tc_offset
     - deblock parameter - tc_offset
     - No
     - No
     - Yes
   * - beta_offset
     - deblock parameter - beta_offset
     - No
     - No
     - Yes
   * - deblock_override
     - Enable deblock override between slice
     - No
     - No
     - Yes
   * - roi_area
     - Specifying rectangular area of CTBs as Region Of Interest with lower QP
     - No
     - No
     - Yes
   * - ipcm_area
     - Area for forcing IPCM macroblocks
     - No
     - No
     - Yes

.. list-table:: Rate Control
   :widths: 20 35 15 15 15
   :header-rows: 1

   * - Name
     - Description
     - Supported
     - Can update at runtime
     - Mandatory
   * - rc_mode
     - Rate control mode (VBR / CVBR / CBR)
     - Yes
     - Yes
     - Yes
   * - picture_rc
     - Adjust QP between pictures
     - Yes
     - Yes
     - Yes
   * - target_bitrate
     - Target bitrate for rate control in bits/second (10000 - 40000000)
     - Yes
     - Yes
     - Yes
   * - qp_hdr
     - Initial target QP, -1 = Encoder calculates initial QP (1-51)
     - Yes
     - Yes
     - Yes
   * - picture_skip
     - Enable Frame Skip
     - No
     - No
     - Yes
   * - intra_pic_rate
     - I frames interval (0 - Dynamic IDR Interval) (0 - 300)
     - No
     - No
     - Yes
   * - ctb_rc
     - Adaptive adjustment of QP inside frame
     - Yes
     - Yes
     - No
   * - block_rc_size
     - Size of block rate control
     - Yes
     - Yes
     - No
   * - hrd
     - Restricts the instantaneous bitrate and total bit amount of every coded picture.
     - Yes
     - Yes
     - No
   * - padding
     - Whether padding on underflow is enabled. Mandatory only for CVBR.
     - Yes
     - Yes
     - No
   * - cvbr
     - Flags to modify rate control behavior
     - Yes
     - Yes
     - No
   * - hrd_cpb_size
     - Buffer size used by the HRD model in bits
     - Yes
     - Yes
     - No
   * - gop_length
     - Rate control uses this to match the average bit rate of each GOP to the target bit rate. affects how fast the rate control reacts to changes in the video sequence. (1 - 300)
     - Yes
     - Yes
     - No
   * - monitor_frames
     - How many frames will be monitored for moving bit rate. Default is using framerate
     - Yes
     - Yes
     - no
   * - bit_var_range_i
     - Percent variations over average bits per frame for I frame
     - Yes
     - Yes
     - No
   * - bit_var_range_p
     - Percent variations over average bits per frame for P frame
     - Yes
     - Yes
     - No
   * - bit_var_range_b
     - Percent variations over average bits per frame for B frame
     - Yes
     - Yes
     - No
   * - tolerance_moving_bitrate
     - Percent tolerance over target bitrate of moving bit rate (0 - 2000)
     - Yes
     - Yes
     - No
   * - variation
     - Allowed variation (%). Relevant for VBR and CVBR. VBR Default: 100. CVBR Default: 15.
     - Yes
     - Yes
     - No
   * - qp_min
     - Minimum frame header QP (0 - 51)
     - Yes
     - Yes
     - No
   * - qp_max
     - Maximum frame header QP (0 - 51)
     - Yes
     - Yes
     - No
   * - intra_qp_delta
     - QP difference between target QP and intra frame QP (-51 - 51)
     - Yes
     - Yes
     - No
   * - fixed_intra_qp
     - Use fixed QP value for every intra frame in stream, 0 = disabled (0 -51)
     - Yes
     - Yes
     - No

.. list-table:: MJPEG Encoder
   :widths: 20 35 15 15 15
   :header-rows: 1

   * - Name
     - Description
     - Supported
     - Can update at runtime
     - Mandatory
   * - n_threads
     - The number of threads to use for the encoding
     - Yes
     - No
     - Yes
   * - quality
     - Quality of encoding
     - Yes
     - Yes
     - Yes

Encoding Level Limitations
--------------------------

In H265 (HEVC) codec - Each level has resolution and bitrate limitations:

  .. csv-table::
    :header: "Level", "Resolution", "Bitrate", "High Tier Bitrate"
    :widths: 10, 20, 15, 20

    "1.0", "QCIF (176x144)", "128 kbits", ""
    "2.0", "CIF (352x288)", "1.5 Mbits", ""
    "2.1", "Q720p (640x360)", "3.0 Mbits", ""
    "3.0", "QHD (960x540)", "6.0 Mbits", ""
    "3.1", "720p HD (1280x720)", "10.0 Mbits", ""
    "4.0", "2Kx1080 (2048x1080)", "12.0 Mbits", "30 Mbits"
    "4.1", "2Kx1080 (2048x1080)", "20.0 Mbits", "50 Mbits"
    "5.0", "4096x2160 (4096x2160)", "25.0 Mbits", "100 Mbits"
    "5.1", "4096x2160 (4096x2160)", "40.0 Mbits", "160 Mbits"
 
For example:
  - Level 4.1 only supports resolution up to 2040X1080 means 3840X2160 is not supported.
  - For Level 5.0, only support resolution up to 4096x2048. And max bitrate of 25Mbits means 30 fps + 3840X2160 resolution is supported.