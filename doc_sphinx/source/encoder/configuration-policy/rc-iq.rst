Encoder Rate Control Quality Parameters
=======================================

-  qp-min

..

   The minimum value QP that can be used by the encoder (best quality)

-  qp-max

..

   The maximum value QP that can be set by the encoder (worst quality)

-  qp-hdr

..

   When RC is enabled, this is the initial QP value with which the
   encoder will start.

   Default value: -1 (automatic, set by resolution and target bitrate).

   In VBR mode, this value is the QP set for the entire stream.

   Must be within the range of qp-min and qp-max.

-  ctb-rc

..

   Enable or disable the ctb-rc feature, recommended to enable this
   feature in CBR/CVBR modes when target bitrate is high enough.

-  intraQpDelta

..

   The intra frames in AVC/HEVC videos can sometimes introduce
   noticeable flickering because of different prediction methods. This
   problem can be overcome by adjusting the quantization of the intra
   frames compared to the surrounding inter frames.

   Valid value range: [-51, 51] (full QP range)

   Default value: 0

   It is not recommended to use values below -5 or above 0.

-  fixed-intra-qp

..

   Use this value for all Intra picture quantization. Value 0 disables
   the feature. Min/Max range checking still applies. intraQpDelta does
   not apply when fixedIntraQp is in use.

   Valid value range: [0, 51]

   Default value: 0

.. raw:: latex

   \clearpage
