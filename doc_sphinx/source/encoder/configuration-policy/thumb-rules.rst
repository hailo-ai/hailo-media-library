‘Rules of Thumb’
================

-  Quantization Parameters (qp-min/max)

   -  Relevant only when RC is enabled (not in VBR mode).

   -  When qp-min is set to low values, underflows are prevented and
      image quality is better but large scene changes increases the
      bitrate for a short while.

   -  When qp-max set to high values, the bitrate target will be kept to
      its limits better, but with noticeable reduced quality on fast scene
      changes.

-  i-frames

   -  Intra frames make video easier to search and edit, but
      they use more bits which decreases the overall quality of the
      stream. Rate control uses the GOP length to match the average bit
      rate of each GOP to the target bit rate. So that the GOP length
      setting affects how fast the rate control reacts to changes in the
      video sequence and encoded bits per frame. The less i-frames the
      better bitrate/quality ratio, but it depends on if high speed
      scene changes require more i-frames.

   -  intra-pic-rate settings should be aimed to be as high as possible
      according to the decoder usage and scene changes. Keeping it 120
      and above gives the best VMAF/SSIM scores in both static and
      moving scenes.

   -  gop-length and monitor frames must be kept as close to the actual
      intra-pic-rate to maintain the bitrate in a single GOP period.
      
      Recommended settings are to use gop-length and monitor-frames equal
      to intra-pic-rate in CBR mode, and 2*intra-pic-rate in CVBR mode.
      See maximum allowed values per argument to keep within limits.
