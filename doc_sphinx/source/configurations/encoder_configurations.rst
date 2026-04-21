.. _overview-configurations-label:

==========================
Encoder-OSD Configurations
==========================

Encoder + OSD
=============

The encoder + OSD configuration is done in a single JSON structure.

The API should receive the JSON as argument, this means the JSON is not read from file by the media library code, the example and/or gst-element should read from file and execute the api.

**Currently only NV12 pipelines are supported for encoding.**

Below JSON snippets represent the video frontend JSON and what they include.

Hailo Encoder JSON example
--------------------------

.. literalinclude:: ../../../hailo-media-library/api/examples/config_examples/encoder_config_example.json
   :language: json

.. _input_stream:

Input Stream
============

.. code-block:: json

    {
        "width": 1920,
        "height": 1080,
        "framerate": 30,
        "format": "NV12",
        "max_pool_size": 5        
    }

- **width**: The width of the input stream in pixels (e.g., 1920).
- **height**: The height of the input stream in pixels (e.g., 1080).
- **framerate**: The number of frames per second (e.g., 30).
- **format**: The format of the input stream (e.g., NV12).
- **max_pool_size**: (Optional) The maximum pool size of the encoder. Note that this is only supported for JPEG encoder at the moment.


.. _hailo_encoder:

Hailo Encoder
=============

Config
-------

.. code-block:: json

    {
        "config": {
            "output_stream": {
                "codec": "CODEC_TYPE_H264",
                "profile": "auto",
                "level": "auto",
                "bit_depth_luma": 8,
                "bit_depth_chroma": 8,
                "stream_type": "bytestream"
            }
        }
    }

- **config**: Configuration for the hailo encoder.

    - **output_stream**: Output stream settings.

        - **codec**: The codec type used. Supported values: ``CODEC_TYPE_H264`` (H.264/AVC) and ``CODEC_TYPE_HEVC`` (H.265/HEVC).
        - **profile**: The profile used (e.g., VCENC_H264_MAIN_PROFILE).
        - **level**: The level used (e.g., 5.1).
        - **bit_depth_luma**: The bit depth for luma (e.g., 8).
        - **bit_depth_chroma**: The bit depth for chroma (e.g., 8).
        - **stream_type**: The stream type (e.g., bytestream).

GOP Config
----------

.. code-block:: json

    {
        "gop_config": {
            "gop_size": 1,
            "b_frame_qp_delta": 0
        }
    }


- **gop_config**: Group of pictures (GOP) configuration.

    - **gop_size**: The GOP size (e.g., 1).
    - **b_frame_qp_delta**: The B-frame QP delta (e.g., 0).


Coding Control
--------------

.. code-block:: json

    {
        "coding_control": {
            "sei_messages": {
                "encoder_timing_sei": true,
                "user_metadata_sei": true
            },
            "deblocking_filter": {
                "type": "DEBLOCKING_FILTER_ENABLED",
                "tc_offset": -2,
                "beta_offset": 5,
                "deblock_override": false
            },
            "intra_area": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "ipcm_area1": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            },
            "ipcm_area2": {
                "enable": false,
                "top": 0,
                "left": 0,
                "bottom": 0,
                "right": 0
            }
        }
    }


- **coding_control**: Coding control settings.

    - **sei_messages**: SEI messages configuration object.

        - **encoder_timing_sei**: Enable encoder's buffering period & picture timing SEI messages (e.g., true).
        - **user_metadata_sei**: Enable custom UUID + JSON metadata SEI (e.g., true).
    - **deblocking_filter**: Deblocking filter settings.

        - **type**: The type of deblocking filter (e.g., ``DEBLOCKING_FILTER_ENABLED``).
        - **tc_offset**: The TC offset value (e.g., -2).
        - **beta_offset**: The beta offset value (e.g., 5).
        - **deblock_override**: Whether deblock override is enabled (e.g., false).
    - **intra_area**: Intra area settings.

        - **enable**: Whether intra area is enabled (e.g., false).
        - **top**: Top position (e.g., 0).
        - **left**: Left position (e.g., 0).
        - **bottom**: Bottom position (e.g., 0).
        - **right**: Right position (e.g., 0).
    - **ipcm_area1**: IPCM area 1 settings.

        - **enable**: Whether IPCM area 1 is enabled (e.g., false).
        - **top**: Top position (e.g., 0).
        - **left**: Left position (e.g., 0).
        - **bottom**: Bottom position (e.g., 0).
        - **right**: Right position (e.g., 0).
    - **ipcm_area2**: IPCM area 2 settings.

        - **enable**: Whether IPCM area 2 is enabled (e.g., false).
        - **top**: Top position (e.g., 0).
        - **left**: Left position (e.g., 0).
        - **bottom**: Bottom position (e.g., 0).
        - **right**: Right position (e.g., 0).



Rate Control
------------

.. code-block:: json

    {
        "rate_control": {
            "rc_mode": "CVBR",
            "picture_rc": true,
            "picture_skip": false,
            "ctb_rc": false,
            "block_rc_size": 64,
            "hrd": false,
            "padding": false,
            "cvbr": 0,
            "hrd_cpb_size": 0,
            "intra_pic_rate": 60,
            "monitor_frames": 0,
            "gop_length": 0,
            "quantization": {
                "qp_min": 10,
                "qp_max": 48,
                "qp_hdr": -1,
                "intra_qp_delta": -5,
                "fixed_intra_qp": 0
            },
            "bitrate": {
                "target_bitrate": 10000000,
                "bit_var_range_i": 2000,
                "bit_var_range_p": 2000,
                "bit_var_range_b": 2000,
                "tolerance_moving_bitrate": 15,
                "variation": 100
            }
        }
    }


- **rate_control**: Rate control settings.

    - **picture_rc**: Whether picture rate control is enabled (e.g., true).
    - **picture_skip**: Whether picture skip is enabled (e.g., false).
    - **ctb_rc**: Whether CTB rate control is enabled (e.g., false).
    - **block_rc_size**: The block rate control size (e.g., 64).
    - **hrd**: Whether HRD is enabled (e.g., false).
    - **padding**: Whether padding on underflow is enabled (e.g., false).
    - **cvbr**: Flags to modify rate control behavior (can be ORed together):
        - **0**: Default behavior.
        - **1**: Asymetric bit allocations for max and min frame size.
        - **2**: Limit QP change between frames for smoother quality changes.
        - **4**: Allow bigger QP changes at the end of the moving window.
        - **8**: Don't allow scene change detection to increase target bits for next frame.
    - **hrd_cpb_size**: The HRD CPB size (e.g., 1000000).
    - **intra_pic_rate**: The intra picture rate (e.g., 60).
    - **monitor_frames**: The number of frames to monitor (e.g., 30).
    - **gop_length**: The GOP length (e.g., 120).
    - **quantization**: Quantization settings.

        - **qp_min**: Minimum QP value (e.g., 20).
        - **qp_max**: Maximum QP value (e.g., 51).
        - **qp_hdr**: Header QP value (e.g., 26).
        - **intra_qp_delta**: Intra QP delta (e.g., -5).
        - **fixed_intra_qp**: Fixed intra QP value (e.g., 0).
    - **bitrate**: Bitrate settings.

        - **target_bitrate**: Target bitrate (e.g., 10000000).
        - **bit_var_range_i**: Bit variance range for I-frames (e.g., 10).
        - **bit_var_range_p**: Bit variance range for P-frames (e.g., 10).
        - **bit_var_range_b**: Bit variance range for B-frames (e.g., 10).
        - **tolerance_moving_bitrate**: Tolerance for moving bitrate (e.g., 0).
        - **variation**: Allowed variation (e.g., 100).


Monitors Control
----------------

.. code-block:: json

    {
        "monitors_control": {
            "bitrate_monitor": {
                "enable": true,
                "period": 3,
                "result_output_path": "bitrate.txt",
                "output_result_to_file": false
            },
            "cycle_monitor": {
                "enable": true,
                "start_delay": 0,
                "deviation_threshold": 5,
                "result_output_path": "cycle.txt",
                "output_result_to_file": false
            }
        }
    }


- **monitors_control**: Monitors control settings.

    - **bitrate_monitor**: Bitrate monitor settings.

        - **enable**: Whether the bitrate monitor is enabled (e.g., true).
        - **period**: The period for the bitrate monitor (e.g., 3).
        - **result_output_path**: The path to output the results (e.g., bitrate.txt).
        - **output_result_to_file**: Whether to output the results to a file (e.g., false).
    - **cycle_monitor**: Cycle monitor settings.

        - **enable**: Whether the cycle monitor is enabled (e.g., true).
        - **start_delay**: The start delay for the cycle monitor (e.g., 0).
        - **deviation_threshold**: The deviation threshold for the cycle monitor (e.g., 5).
        - **result_output_path**: The path to output the results (e.g., cycle.txt).
        - **output_result_to_file**: Whether to output the results to a file (e.g., false).

.. _smart_encoder:

Smart Encoder
-------------

.. code-block:: json

    {
        "smart_encoder": {
            "enabled": true,
            "background_qp_delta": 10,
            "rois": [
                {
                    "x": 0.1,
                    "y": 0.05,
                    "width": 0.2,
                    "height": 0.15
                },
                {
                    "x": 0.3,
                    "y": 0.2,
                    "width": 0.1,
                    "height": 0.1
                }
            ]
        }
    }


- **smart_encoder**: Smart encoding settings for Region of Interest (ROI) prioritization. Allows configuring one or more high-priority regions that maintain higher visual quality while background areas are compressed more aggressively to save bitrate.

    - **enabled**: Master switch for the smart encoder feature (boolean, default: ``false``). When ``true``, background regions are encoded with increased QP (reduced quality) while ROI regions are encoded at full quality. This feature is only supported with H.264 codec and CVBR rate control mode.
    - **background_qp_delta**: QP (Quantization Parameter) delta applied to all frame regions not covered by a defined ROI. Higher values increase compression and reduce quality in background areas, freeing bitrate for ROI regions. Valid range: 0–16 (default: 10).
    - **rois**: Array of Regions of Interest (maximum 10 per frame, default: []). Each ROI specifies a rectangular region with normalized coordinates relative to frame dimensions, making configuration resolution-independent. Each ROI object contains:

        - **x**: Normalized x coordinate of the ROI top-left corner (float, range [0.0, 1.0]). 0.0 = left edge, 1.0 = right edge.
        - **y**: Normalized y coordinate of the ROI top-left corner (float, range [0.0, 1.0]). 0.0 = top edge, 1.0 = bottom edge.
        - **width**: Normalized width of the ROI (float, range [0.0, 1.0]).
        - **height**: Normalized height of the ROI (float, range [0.0, 1.0]).

    Configuration Rules:
        - ROIs must not extend beyond frame boundaries (x + width ≤ 1.0, y + height ≤ 1.0).
        - Smart encoder can only be enabled when using H.264 codec and CVBR rate control mode.
        - The feature can be configured via the Profile Manager tool and changed at runtime if required.


JPEG Encoder JSON example
-------------------------

.. code-block:: json

    {
        "jpeg_encoder": {
            "n_threads": 1,
            "quality": 85
        }
    }


- **jpeg_encoder**: JPEG related configuration.
    - **n_threads**: The number of threads to use for the encoding.
    - **quality**: Quality of encoding.

Restrictions
------------

* Input and output resolutions and format: are constant, and cannot be changed during pipeline running
* Codec type can be changed during pipeline running

API Reference
-------------

:ref:`encoder-osd-label`.
