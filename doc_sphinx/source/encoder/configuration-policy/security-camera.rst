Security Camera Configuration
=============================

-  In general, VBR configuration should be sufficient for a security
   camera for both static and dynamic scenes.

-  When a camera is static the bitrate will usually be equal or below
   the target bitrate.

-  Rate control updates are available via the low-level API, gstreamer
   elements and media-library.

-  User can set different rate control configuration for different
   camera states (static/moving) during execution.
