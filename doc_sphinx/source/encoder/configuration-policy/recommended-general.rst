Recommended Settings (General)
==============================

There are several use cases for video encoding, systems differ from one
another in terms of video quality and bitrate limitations:

-  **Local playback** - no storage/bitrate limitations, video quality is
   the main factor.

-  **Internal streaming** - bitrate is restricted (yet some variation is
   allowed), this means video quality needs to be maintained within the
   boundaries.

-  **Real-time streaming** - bitrate restrictions must be enforced, even
   at the cost of losing frames.

Recommended Rate Control Settings (General)
-------------------------------------------

The table below describes the recommended ranges for the above parameters per
the application requirements and device capabilities.

.. csv-table:: Recommended rate control settings
   :file: recommended-general.csv
   :header-rows: 1

Recommended Rate Control Settings (Detailed)
--------------------------------------------

-  **General Settings**

   -  Use ctb-rc for better quality and bitrate control.
      This feature is recommended to be enabled when target bitrate is
      high enough to cope with internal qp changes.
      When enabled in low bitrate domains, large movement will produce
      more trailing artifacts due to the limited bitrate.

   -  Set gop-length and monitor-frames to intra-rate (CBR) or
      intra-rate*2 (CVBR) to maintain the bitrate in a single GOP period.
      See maximum allowed values per argument to keep within limits.

   -  Set hrd and hrd-cpb-size according to the decoder buffer size
      (instead of default 10M).

   -  tol-moving-bitrate should be set according to the RC mode (see
      below).

   -  bitVarRangeI should be set to a low value in CBR mode (10) and higher
      in CVBR mode (100-2000).
      
   - bitVarRangeP/bitVarRangeB should be set to a high value (2000) to
     allow RC to adjust the bitrate on these frames according to the
     encoding policy.

   - block-rc-size
     For AVC use block-rc-size of 0 (16x16).
     For HEVC use block-rc-size of 2 (64x64).

   - hrd-cpb-size
     The default hrd-cpb-size is 10M. When HRD is enabled, set it to the decoder bucket size.

   - Padding
     Padding may be enabled in CBR/HRD mode to disallow underflows.

   - Profile and Level
     The encoding profile and level are set according to the encoded resolution, see table below.

     .. list-table:: Profile and Level per resolution (AVC/HEVC)
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

-  **CBR Mode**

   -  Bitrate shall match the configured target in 1 GOP interval (moving average bitrate is less of a concern)

   -  picture-rc is enabled

   -  ctb-rc is enabled only when target bitrate is high enough to cope with internal qp changes

   -  The Limit variable (in the table below) stands for the fluctuation of the bitrate above and below the target bitrate in percentage, minimum limitation is 15%, maximum is 2000%

   -  User may set a permitted overflow above the target bitrate (default is 0) however it is unlikely it will be used

   -  HRD and padding may be enabled to work in conjunction with CBR to create a stricter bitrate (dropping frames upon overflow, and padding with zeros when underflow)
      hrd-cpb-size shall be configured according to the decoder bucket size (instead of default 10M)

   -  See below tables for specific configuration per resolution/bitrate

   -  Limit stands for the allowed fluctuation below/above the target bitrate (percentage) for a GOP period

      - Minimum value is 15%

      - bitVarRangeI is equal to (limitation – 5)

-  **CVBR Mode**

   -  Bitrate shall not exceed the target bitrate by more than the allowed variation value, but underflows are allowed to save storage/network bandwidth on static/slow scenes

   -  While similar to CBR mode, bitrate fluctuations are allowed to adapt to scene changes better

   -  Allowing underflows is done by restricting the qp value from dropping below a certain value (depending on target bitrate), and setting a minimal compression ratio on all frames

   -  Typical use case is set specific target bitrate, and allow additional overflow to handle scene changes (variation in the table below)

   -  This feature may need tuning of qp-min per camera, scene type, and image-quality factor

   -  See below tables for specific configuration per resolution/bitrate

   -  Variation stands for the limitation that RC can overflow above the target bitrate (percentage)

      - Range is 0-2000

      - Variation is inserted "as is" into the tol-moving-bitrate parameter

-  **VBR Mode**

   -  User needs to set the desired encoding quality, no rate control is enabled in this mode

   -  picture-rc is disabled

   -  ctb-rc is enabled for better quality

   -  qp-min and qp-max allow maximum range (1-51)

   -  qp-hdr sets the desired quality of the video, recommended values 21-30

   -  intra-qp-delta shall be no more than -5 (recommended default value)

   -  High bitrate is expected (at least 20Mbps in 4K)

