Encoder Rate Control Configuration Parameters
=============================================

-  picture-rc

..

   Enable or disable rate control.

-  gop-length

..

   Rate control uses the GOP length to match the average bit rate of
   each GOP to the target bit rate. Therefore the GOP length setting
   affects how fast the rate control reacts to changes in the video
   sequence and encoded bits per frame. e.g. a GOP length of 30 frames
   on a 30 fps stream will match the target bitrate every second,
   whereas a GOP length of 150 frames will match the target bitrate over
   five seconds.

   Recommended value is intra-pic-rate (to maintain the bitrate per GOP).
   This variable shall be a multiplication of intra-pic-rate to include 
   a fixed number of intra-frames.

-  monitor-frames

..

   Specifies how many frames will be monitored for moving bit rate.

   By default, this variable is set to the intra-pic-rate (CBR) or
   2*intra-pic-rate (CVBR).
   This variable shall be a multiplication of intra-pic-rate to include 
   a fixed number of intra-frames.

-  hrd

..

   Enables the use of a Hypothetical Reference Decoder (HRD) model to
   restrict the instantaneous bitrate.

   Enabling HRD will force the configured bitrate as much as possible
   and try to comply with a stricter bitrate policy. It might drop
   frames in a case where the bitrate exceeds the target bitrate.

-  hrd-cpb-size

..

   The size of the theoretical buffer is indicated by the hrd-cpb-size
   parameter.

   Relevant only for HRD RC mode.

   Normal value would be the target bitrate (has to be configured by the
   user).

-  tol-moving-bitrate

..

   Percentage tolerance over target bitrate for a moving bit rate.

-  bitVarRangeI/bitVarRangeP/bitVarRangeB

..

   Percentage variations over average bits per frame for I/P/B frame.

   - bitVarRangeI
   
   ..
      
   Variation for I-frames, is set to minimum in CBR mode (10)
   and higher in CVBR mode (2000).

   - bitVarRangeP/bitVarRangeB
   
   ..
      
   Variation for P/B frames, is set to 2000 to allow RC to adjust the
   bitrate on these frames according to the encoding policy.

-  cvbr

..

   Runtime flag for bits allocation and QP variation between frames.

   Default value: 0

   See presets tables for recommended value per mode/bitrate.

-  padding

..

   When the error from the theoretical buffer is smaller than the
   average frame size (for example, 1M bitrate at 30fps -> avg. frame
   size = 33.3K), padding is added to the frame to prevent underflow of
   the theoretical buffer.

   This is available for all RC modes.

.. raw:: latex

   \clearpage
