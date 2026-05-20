Overview
========

This chapter describes the Application Programming Interface (API) of
the HEVC/AVC video encoder hardware.

The description of the Video Encoder API are described in the following order:

-  Components such as data types, return codes, enumerations and
   relevant structures.

-  The Function syntax and their related descriptions.

-  An overview of the programming sequence with examples.
   The code is written in C and parameter types follow the standard C conventions.
   This document assumes that the reader understands the fundamentals of C-language
   and the HEVC (H.265)/AVC (H.264) standards.
   Currently, there are no deprecated functions in this API.

The code is written in C and parameter types follow standard C
conventions. This document assumes that the reader understands the
fundamentals of C-language and the HEVC (H.265)/AVC (H.264) standards.

Currently, there are no deprecated functions in this API.

Supported Standards
-------------------

The API discussed in this version of the document are compatible with
the following video encoder standards and profiles. Additional levels
may be supported via software.

-  HEVC (H.265) - ITU-T Rec. H.265 (04/2013), ISO/IEC 23008-2 9

   -  Main Profile, Level 5.1, High Tier

   -  Main10 profile, Level 5.1, High Tier

   -  Main Still Profile

-  AVC (H.264) - ITU-T Rec. H.264 (03/2010) / ISO/IEC 14496-10

   -  Main Profile, levels 1 - 5.2

   -  High Profile, levels 1 - 5.2

   -  High 10 Profile, levels 1 - 5.2

Compatible Hardware:
--------------------

-  VC8000E Video Encoder v6.0.00 or later

Standard References
-------------------

`ITU-T
H.265 <https://www.itu.int/ITU-T/recommendations/rec.aspx?rec=11885>`__.
High Efficiency Video Coding

`ITU-T
H.264 <http://www.itu.int/itu-t/recommendations/rec.aspx?rec=H.264>`__.
Advanced video coding for generic audiovisual services

`ITU-R-REC-BT.601 <http://www.itu.int/rec/R-REC-BT.601/en>`__. Studio
encoding parameters of digital television for standard 4:3 and wide
screen 16:9 aspect ratios.

`ITU-R-REC-BT.709 <http://www.itu.int/rec/R-REC-BT.709/en>`__ Parameter
values for the HDTV standards for production and international programme
exchange.
