Rate Control
============

The encoding bitrate and quality are controlled by setting QP value to
the hardware.

The rate control is done by the software (on the A53) by adjusting the
QP value per frame (in CBR and CVBR modes).

**Rate Control (RC)**

In general, the software RC algorithm implements the R-Q model to
adaptively adjust the QP value per frame to maintain a target bitrate.

The intra-frame QP value is set according to the previous GOP's average QP,
and the inter-frame QP value is set according to the previous frame's QP value.

There are several RC modes available, which differ in the way they configure
the algorithm's tolerance to changes, thresholds and QP step sizes.

The QP range for RC operations is between configured qp-min and qp-max.

Additionally, several other features and configuration methods are
available to adjust the RC for specific behavior.

Hailo supports the following encoding RC modes:

-  Constant Bitrate (CBR)

   The RC algorithm always aims to maintain the target bitrate.

   In CBR mode, the RC adapts and corrects the bitrate and quality to
   compensate overflows and underflows in previous frames and GOPs.
   There are restrictions on moving average overflows and underflows to
   maintain accurate target bitrate.

   This is the stricter RC configuration, and it allows minimal tolerance
   to bitrate variations.

   In this mode, the QP range is wide to avoid overflows and underflows.
   If the qp-min is too high, the encoder may not be able to achieve the
   minimal target bitrate.

   User may set symmetric overflow and underflow percentage to allow
   some flexibility in the bitrate variations.
   Minimal variation is 15% and maximal is 2000%.
   The default is 15% (minimal).

   Overflows and underflows are being kept in the user specific
   boundaries (allowed bitrate variation). Boundaries are maintained
   within a GOP period.

..

-  Constrained Variable Bitrate (CVBR)

..

   This mode is less strict than CBR, and allows more flexibility in
   achieving target bitrate and fluctuations in bitrate per scene.

   In CVBR mode, the RC may adapt and correct the bitrate and quality to
   compensate overflows in previous frames and GOPs.
   There are restrictions on moving average overflows to
   maintain accurate target bitrate.

   In this mode, the user sets the minimal image-quality required
   (minimal QP value), which is higher than the qp-min value of the CBR to
   allow saving bitrate bandwidth when possible.

   The CVBR presets differ in the qp-min value to allow underflows and maintain the
   desired quality, and the user can also override it if a higher or lower image-quality is required.

   A typical use case is setting the target bitrate to the desired average
   and allowing additional bandwidth using the tolMovingBitrate parameter for
   sudden fast motions.
   For example, setting the target bitrate to 4.8Mbps and allowing additional
   25% tolerance will allow the encoder to reach up to 6Mbps for short
   periods of time, while maintaining the average target bitrate of 4.8Mbps
   or less when scene is static.

..

-  Variable Bitrate (VBR)

..

   The simplest quality-only configuration is VBR, in which RC is
   disabled, and the user sets the QP value for the entire stream. Unless
   changed by a user, the QP value remains constant for the entire stream.

   Output bitrate is not controlled while streaming, and is only affected by
   the QP value and the scene complexity.

   This mode might be in use with local networks, where the bitrate is less
   significant (live stream without storage, etc).

   Note that the CTB feature must be disabled in this mode.

..

**Coding Tree Block (CTB) Feature**

   Standalone RC feature (regardless of actual configured RC mechanism),
   which allows setting a different QP value per CTB inside a frame.

   Enabling ctb-rc allows the encoder to decrease QP value on flat
   surfaces to increase quality on these regions and reduce encoding
   effects.

   As this feature may increase quality on specific regions in the image,
   it is recommended to use only when target bitrate is high enough.

   This feature may be enabled in CBR and CVBR modes, and controlled by
   the ctb-rc configuration parameter.

**Smart ROI Encoder**

   The Smart ROI Encoder allows users to mark high-priority regions in a video frame.
   ROI areas maintain higher visual quality, while background areas use a higher QP
   (lower quality) to reduce bitrate without sacrificing important details.

   This feature enables efficient bitrate allocation by concentrating quality in user-defined
   Regions of Interest (ROIs), while allowing the encoder to compress background regions
   more aggressively. The background QP delta parameter controls the quality tradeoff:
   higher delta values increase compression in background areas, freeing bitrate for ROI
   regions to encode at the base QP level.

   Up to 10 ROI regions can be defined per frame using normalized coordinates (0.0–1.0),
   making it easy to specify regions of interest relative to the frame dimensions regardless
   of input resolution.

   **Compatibility:** The Smart ROI Encoder is supported in CVBR mode only with H.264 codec.
   ROI regions can be configured through the Camera Viewer web UI (by drawing bounding boxes)
   or directly in the encoder JSON configuration under the ``smart_encoder`` section.

   **Limitation:** In heavy motion, complex scenes, or when ROI areas are very large,
   this feature may not result in significant bitrate savings due to the complexity of allocating
   quality appropriately across the frame.

.. raw:: latex

   \clearpage
