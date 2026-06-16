.. _overview-configurations-label:

==========================
Encoder Configurations
==========================

The encoder configuration is done in a single JSON structure.

The API should receive the JSON as argument, this means the JSON is not read from file by the media library code, the example and/or gst-element should read from file and execute the api.

Below JSON snippets represent the video frontend JSON and what they include.

Encoder JSON example
--------------------------

.. literalinclude:: ../../../hailo-media-library/api/examples/config_examples/encoder_config_example.json
   :language: json

.. _hailo_encoder:

Hailo Encoder
-------------

Input Stream
~~~~~~~~~~~~

.. code-block:: json

    {
        "width": 1920,
        "height": 1080,
        "framerate": 30,
        "format": "NV12",
        "max_pool_size": 5
    }

- **width**: The width of the input stream in pixels.
- **height**: The height of the input stream in pixels.
- **framerate**: The number of frames per second.
- **format**: The format of the input stream. Currently only NV12 pipelines are supported for encoding.
- **max_pool_size**: (Optional) The maximum pool size of the encoder. Note that this is only supported for JPEG encoder at the moment.




Config
~~~~~~

Specifies the codec, profile, and level for the generated stream. Hailo supports
**H.264 (AVC)** and **H.265 (HEVC)**. By default ``profile`` and ``level`` are set to
``"auto"`` — the encoder picks them based on the encoded resolution (see table below).

**Supported profiles:**

- **AVC**: Baseline, Main, High.
- **HEVC**: Main.

.. list-table:: AVC (H.264) — Profile and Level per resolution
   :header-rows: 1
   :align: center

   * - Resolution
     - Profile
     - Level
   * - up to 480p
     - Main
     - 3.0
   * - up to 720p
     - High
     - 3.1
   * - up to 1080p
     - High
     - 4.1
   * - up to 4K
     - High
     - 5.1

.. list-table:: HEVC (H.265) — Profile and Level per resolution
   :header-rows: 1
   :align: center

   * - Resolution
     - Profile
     - Level
   * - up to 720p
     - Main
     - 4.0
   * - up to 1080p
     - Main
     - 4.1
   * - up to 4K
     - Main-10
     - 5.1

.. only:: html

   For the full level/bitrate reference (every H.264 and H.265 level with its maximum
   resolution, frame rate, and bitrate), see :ref:`h264_hevc_levels_table`.

.. only:: latex

   For the full level/bitrate reference, see the table below:

   .. raw:: latex

      \begingroup\footnotesize

   .. csv-table:: HEVC and H.264 Levels and their Typical Encoding Values
      :file: ../encoder/hevc_and_h264_levels.csv
      :header-rows: 1
      :widths: 22 14 8 14 14 14

   .. raw:: latex

      \endgroup

.. code-block:: json

    {
        "config": {
            "output_stream": {
                "codec": "CODEC_TYPE_H264",
                "profile": "auto",
                "level": "auto"
            }
        }
    }

- **config**: Codec and stream configuration.

    - **output_stream**:

        - **codec**: ``CODEC_TYPE_H264`` (H.264/AVC) or ``CODEC_TYPE_HEVC`` (H.265/HEVC).
        - **profile**: (Optional) H.264/H.265 profile. ``"auto"`` selects from the table above.
        - **level**: (Optional) H.264/H.265 level. ``"auto"`` selects from the table above.

GOP Config
~~~~~~~~~~

GOP is a group of successive pictures within a video stream. GOP usually
contains an Intra frame (I-frame) and several Inter frames (Predicted
frame). Intra frames allow the video to be more easily searched and
editable, however they use more bits which decreases the overall quality
of the stream.


.. code-block:: json

    {
        "gop_config": {
            "gop_size": 1,
            "b_frame_qp_delta": 0
        }
    }


- **gop_config**:

    - **gop_size**: Number of frames in each P+B sub-group between two P-frames (or between
      an I-frame and the next P-frame). The pattern is ``P`` followed by ``gop_size - 1``
      B-frames, e.g. ``gop_size = 4`` produces ``I - P - B - B - B - P - B - B - B - P - ...``.
      Range ``1–7`` (``1`` = no B-frames). Not to be confused with ``intra_pic_rate``, which
      sets the I-frame interval (total frames per GOP).
    - **b_frame_qp_delta**: QP difference between B-frame QP and target QP.
      ``-1`` = disabled; otherwise range ``1–51``.


Coding Control
~~~~~~~~~~~~~~

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


- **coding_control**:

    - **sei_messages**: SEI messages configuration object.

        - **encoder_timing_sei**: Enable encoder's buffering period & picture timing SEI messages.
        - **user_metadata_sei**: Enable custom UUID + JSON metadata SEI.

    - **deblocking_filter**: Deblocking filter settings.

        - **type**: The type of deblocking filter (e.g., ``DEBLOCKING_FILTER_ENABLED``).
        - **tc_offset**: The TC offset value.
        - **beta_offset**: The beta offset value.
        - **deblock_override**: Whether deblock override is enabled.

    - **intra_area** / **ipcm_area1** / **ipcm_area2**: Force a rectangular region of the
      frame to be encoded with a specific mode — ``intra_area`` forces intra-block encoding;
      ``ipcm_area1`` and ``ipcm_area2`` force uncompressed IPCM encoding. Each area uses the
      same fields:

        - **enable**: Activate the area.
        - **top**, **left**, **bottom**, **right**: Rectangle bounds (pixels).


Rate Control
~~~~~~~~~~~~

The software rate-control (RC) algorithm uses the R-Q model to adjust the QP per frame
and hit a target bitrate. Intra-frame QP is derived from the previous GOP's average QP;
inter-frame QP is derived from the previous frame's QP. The QP range is bounded by
``qp_min`` and ``qp_max``. Hailo supports three RC modes — CBR, CVBR, and VBR — described below.

-   **Constant Bitrate (CBR)**

    The strictest RC configuration. Always aims to maintain the target bitrate by adjusting
    QP per frame to compensate for over- and underflows in the previous GOP.

    The QP range is kept wide to avoid overflows; if ``qp_min`` is set too high the encoder may
    not reach the minimum target bitrate. Bitrate variation is enforced within each GOP period.

    - ``picture_rc`` is enabled.
    - ``ctb_rc`` is enabled only when the target bitrate is high enough to absorb internal QP changes.
    - Optionally combine with ``hrd`` + ``padding`` for the strictest enforcement: ``hrd`` drops
      frames on overflow, ``padding`` adds zero-bytes on underflow. Set ``hrd_cpb_size`` to the
      decoder bucket size (instead of the default 10M).

-   **Constrained Variable Bitrate (CVBR)**

    Less strict than CBR — allows bitrate fluctuations between scenes while keeping the moving
    average close to the target bitrate.

    The user sets a minimal image-quality (a ``qp_min`` higher than CBR's) so the encoder can
    save bandwidth on static scenes. Burst headroom for fast motion is set with
    ``tolerance_moving_bitrate``.

    May need tuning of ``qp_min`` per camera, scene type, and desired image quality.

-   **Variable Bitrate (VBR)**

    Quality-only configuration — RC is disabled. The user sets a constant QP via ``qp_hdr``
    (recommended ``21–30``) and the encoder keeps it for the entire stream. Output bitrate is
    uncontrolled and depends on scene complexity, so expect high bitrate (≥ 20 Mbps in 4K).

    - ``picture_rc`` is disabled.
    - ``ctb_rc`` must be disabled.
    - ``qp_min`` and ``qp_max`` allow the full range (``1–51``).
    - ``intra_qp_delta`` should not be below ``-5`` (recommended default).

-   **Coding Tree Block (CTB) Feature**

    Standalone feature that sets a per-CTB QP inside a frame, redistributing bits closer to
    perceptual quality — flat surfaces (where artifacts are most visible to the human eye)
    get a lower QP and more bits, while busier regions can tolerate higher QP without
    noticeable degradation. Controlled by ``ctb_rc``.

    Enable in CBR/CVBR when the target bitrate is high enough to absorb the QP changes.
    At low bitrates, enabling CTB causes trailing artifacts on motion. Must be disabled in VBR.

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


- **rate_control**: 

    - **rc_mode**: One of ``CBR``, ``CVBR``, ``VBR``.
    - **picture_rc**: (Optional) Enable per-picture rate control. Disabled in VBR.
    - **picture_skip**: (Optional) Allow the encoder to skip pictures to maintain bitrate.
    - **ctb_rc**: (Optional) Enable per-CTB QP adjustment. Must be ``false`` in VBR.
    - **block_rc_size**: (Optional) Block size for CTB rate control. AVC: ``0`` (16x16). HEVC: ``2`` (64x64).
    - **hrd**: (Optional) Enable Hypothetical Reference Decoder model to restrict instantaneous bitrate.
      Drops frames if bitrate exceeds target. Pair with ``hrd_cpb_size`` set to the decoder buffer.
    - **hrd_cpb_size**: (Optional) HRD coded picture buffer size in bits. Default 10M. When ``hrd`` is
      enabled, set to the decoder bucket size (typically the target bitrate).
    - **padding**: (Optional) Pad frames when the buffer error is smaller than the average frame size, to
      prevent underflow. Use in CBR/HRD mode.
    - **cvbr**: (Optional) Bit flags fine-tuning CVBR behavior (OR together):

        - **0**: Default.
        - **1**: Asymmetric bit allocation for max/min frame size.
        - **2**: Limit QP change between frames for smoother quality.
        - **4**: Allow bigger QP changes at the end of the moving window.
        - **8**: Don't allow scene-change detection to increase bits for the next frame.

    - **intra_pic_rate**: I-frame interval (frames). Range ``0–300``.
    - **gop_length**: (Optional) Rate-control window length. Range ``1–300``. Must be a multiple
      of ``intra_pic_rate``; recommended equal to ``intra_pic_rate`` for CBR, or
      twice ``intra_pic_rate`` for CVBR.
    - **monitor_frames**: (Optional) Number of frames tracked for the moving bitrate calculation.
      Default: ``intra_pic_rate`` (CBR) or twice ``intra_pic_rate`` (CVBR).
    - **quantization**: 

        - **qp_min**: (Optional, recommended for CBR/CVBR) Lower bound on QP (best quality).
          Range ``0–51``. Low values prevent underflow but allow bitrate spikes on scene changes.
        - **qp_max**: (Optional, recommended for CBR/CVBR) Upper bound on QP (worst quality).
          Range ``0–51``. High values keep bitrate close to target but reduce quality on fast scenes.
        - **qp_hdr**: (Optional, required in VBR) Initial QP. ``-1`` = automatic (set by
          resolution + target bitrate). In VBR this is the constant QP for the entire stream.
          Must lie within ``[qp_min, qp_max]``.
        - **intra_qp_delta**: (Optional) QP offset for intra frames, to reduce flicker between
          intra/inter predictions. Range ``[-51, 51]``, default ``0``. Values below ``-5``
          or above ``0`` are not recommended.
        - **fixed_intra_qp**: (Optional) Fixed QP for all intra frames. ``0`` disables the
          feature. Range ``[0, 51]``. Overrides ``intra_qp_delta`` when in use.

    - **bitrate**: 

        - **target_bitrate**: (Optional, required for CBR/CVBR) Target bitrate in bits per second.
          Range ``10000–40000000``.
        - **bit_var_range_i**: (Optional) Allowed bit-variance for I-frames (%). Low (10) in CBR;
          higher (100–2000) in CVBR.
        - **bit_var_range_p**: (Optional) Allowed bit-variance for P-frames (%). High (2000)
          to let RC adjust per encoding policy.
        - **bit_var_range_b**: (Optional) Allowed bit-variance for B-frames (%). High (2000)
          as for P-frames.
        - **tolerance_moving_bitrate**: (Optional) Percentage tolerance over target bitrate for
          the moving bitrate window. Range ``0–2000``. Set per RC mode.
        - **variation**: (Optional) Maximum percentage the RC may overflow above target bitrate.
          Range ``0–2000``. Defaults: ``100`` (VBR), ``15`` (CVBR). Passed as-is to
          ``tolerance_moving_bitrate`` in CVBR mode.


Monitors Control
~~~~~~~~~~~~~~~~

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


- **monitors_control**: 

    - **bitrate_monitor**: 

        - **enable**: Whether the bitrate monitor is enabled.
        - **period**: The period for the bitrate monitor.
        - **result_output_path**: The path to output the results (e.g., bitrate.txt).
        - **output_result_to_file**: Whether to output the results to a file.

    - **cycle_monitor**:

        - **enable**: Whether the cycle monitor is enabled.
        - **start_delay**: The start delay for the cycle monitor.
        - **deviation_threshold**: The deviation threshold for the cycle monitor.
        - **result_output_path**: The path to output the results (e.g., cycle.txt).
        - **output_result_to_file**: Whether to output the results to a file.


Smart Encoder
~~~~~~~~~~~~~

**SmartStream+** is Hailo's bitrate-saving feature: it concentrates quality in user-defined
Regions of Interest (ROIs) while compressing background regions more aggressively, reducing
overall bitrate without sacrificing perceptual quality in the areas that matter.
The users can define bounding boxes or select classes to be detected using AI.

-   **Compatibility:** Supported in CVBR mode and H.264 only. 

-   **Limitation:** In heavy motion, complex scenes, or when ROI areas are very large,
    this feature may not result in significant bitrate savings.

-   **Configuration rules:**
    - ROIs must not extend beyond frame boundaries (``x + width ≤ 1.0``, ``y + height ≤ 1.0``).
    - Setting a higher ``qp_max`` is recommended for optimal feature performance.

.. code-block:: json

    {
        "smart_encoder": {
            "enabled": true,
            "background_qp_delta": 15,
            "rois": [
                {
                    "x": 0.1,
                    "y": 0.1875,
                    "width": 0.8,
                    "height": 0.625
                }
            ],
            "analytics_labels": ["person"]
        }
    }


- **smart_encoder**: 

    - **enabled**: Master switch for the smart encoder feature (boolean, default: ``false``). When ``true``, background regions are encoded with increased QP (reduced quality) while ROI regions are encoded at full quality.
      This feature is only supported with H.264 codec and CVBR rate control mode.
    - **background_qp_delta**: QP (Quantization Parameter) delta applied to all frame regions not covered by a defined ROI. Higher values increase compression and reduce quality in background areas, freeing bitrate for ROI regions. Valid range: 0–16 (default: 10).
    - **analytics_labels**: List of analytics class labels whose AI detections are
      automatically added as dynamic ROIs each frame. Supported classes: ``"person"``,
      ``"vehicle"``, ``"face"``, ``"license_plate"``. Combines with the static ``rois`` list —
      both apply. Empty array (``[]``) disables detection-driven ROIs.
    - **rois**: Array of Regions of Interest (maximum 10 per frame, default: []). Each ROI specifies a rectangular region with normalized coordinates relative to frame dimensions, making configuration resolution-independent. Each ROI object contains:

        - **x**: Normalized x coordinate of the ROI top-left corner (float, range [0.0, 1.0]). 0.0 = left edge, 1.0 = right edge.
        - **y**: Normalized y coordinate of the ROI top-left corner (float, range [0.0, 1.0]). 0.0 = top edge, 1.0 = bottom edge.
        - **width**: Normalized width of the ROI (float, range [0.0, 1.0]).
        - **height**: Normalized height of the ROI (float, range [0.0, 1.0]).

JPEG Encoder
~~~~~~~~~~~~

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


API Reference
-------------

:ref:`encoder-label`
