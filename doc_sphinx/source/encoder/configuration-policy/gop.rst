General Encoder GOP
===================

GOP is a group of successive pictures within a video stream. GOP usually
contains an Intra frame (I-frame) and several Inter frames (Predicted
frame). Intra frames allow the video to be more easily searched and
editable, however they use more bits which decreases the overall quality
of the stream.

User’s GOP configuration includes the following parameters:

-  intra-pic-rate

..

   Intra-frame (i-frame) interval for the stream.

-  gop-size

..

   Number of b-frames (including the following p-frame).

   Examples:

-  intra-pic-rate 10, GOP Size of 1: i-p-p-p-p-p-p-p-p-p …
   i-p-p-p-p-p-p-p-p-p

-  intra-pic-rate 10, GOP Size of 3: i-b-b-p-b-b-p-b-b-p …
   i-b-b-p-b-b-p-b-b-p

-  Bitrate

..

   Target bitrate.

-  Profile

..

   Specifies the HEVC(H.265)/AVC(H.264) profile of the generated stream.

-  AVC allows the following profiles:

   -  Main, used for standard-definition digital TV broadcasts that use
      the MPEG-4 format.

   -  High, the primary profile for broadcast and disc storage
      applications, particularly for high-definition TV applications.

   -  | High-10, this profile builds on top of the High Profile, adding
        support for up to
      | 10-bits per sample

-  HEVC allows the following profiles:

   -  Main, allows for a bit depth of 8-bits per sample with 4:2:0
      chroma sampling.

   -  Main-10, Allows for a bit depth of 8-bits to 10-bits per sample
      with 4:2:0 chroma sampling.

-  Level

..

   Specifies the HEVC(H.265)/AVC(H.264) level of the generated stream.

   See the API manual for specific level per resolution and target
   bitrate.
