Recommended Configuration - Static Camera
=========================================

-  Use case is recommended for static camera.

-  Intra-pic-rate may be up to 4 seconds (120 when input is 30 fps).

-  GOP size may be up to 8 (7 b-frames), the recommended value is 4.

Recommended Configuration - Moving Camera
=========================================

-  Recommended for moving camera and strict BR.

-  Intra-pic-rate may be up to 2 seconds (60 when input is 30 fps),
   depends on the scene change speed.

-  GOP size may be up to 4, the recommended value is 4.

-  hrd may be enabled - will keep target bitrate more accurately but may
   drop frames when stream exceeds target bitrate.
