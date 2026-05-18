Introduction
============

The following manual describes the H-15 AVC/HEVC encoder stack internals,
low level API, and functionalities.

Most user applications shall use the Hailo Media Library APIs, which provides
further abstraction on top of the low level encoder stack.
The main functionalities of the encoder are interfaced in the media-library
:ref:`encoder block section <encoder-osd-label>`, with much more convenient and
easy to use API and also provides pipeline integration with the
Media Library frontend.

Moreover, most of media-library demo applications already includes encoding
and the reference code how to use it.
Hailo recommends to use the media-library for encoding smooth experience.

The underlying low-level API are also available, which allows users to test
the encoding capabilities offline, and adopt the encoder stack to their own
software architecture if Media Library is not being used.

Encoder Workflow (Low Level)
============================

The block diagram of a typical encoding process is shown in the figure
below. The flowchart below shows the main steps to use the encoder: the
initialization, the configuration (optional), the stream production
(start, frame encoding and end) and finally the release of the encoder.

.. _fig_encman_process:

.. figure:: /_images_src/fig_encman_process.png
   :alt: Encoding Process Block Diagram
   :align: center
   :width: 60%

   Encoding Process Block Diagram

.. raw:: latex

   \clearpage

Encoder Limitations
===================

The pre-processing block has some limitations on the input picture size
and address. The pre-processor will return failure if the application
doesn't conform to these limitations. The initialization will also fail
if the application tries to use an encoding feature not supported by the
hardware.

Initialization and Release of the Encoder
=========================================

In order to be able to use the encoder, it must be properly initialized
first. The initialization is done by calling
:ref:`VCEncInit. <vcencinit>`\ () This call will allocate all the resources
needed by the encoder and will execute the necessary setup steps in
order to have a fully operational encoder. If successful, the
initialization call will return a new instance of the encoder, which
will be used as an identifier for all subsequent encoder operations.

The encoder product has support for multiple software instances sharing
a single encoder hardware. This means that it is possible to initialize
several encoder instances and use them for encoding different streams.
The only limitation is that the hardware can only encode one frame at a
time so the encoder instances share the hardware with time slicing
principle.

The initialization call will return VCENC_OK if the initialization is
successful. The error code returned in case of a failure will indicate
what went wrong during the initialization process.

Certain parameters must be set when initializing the encoder. These
parameters cannot be changed during the encoding process.

Stream Profile and Level Indication
-----------------------------------------

The HEVC standard defines Profiles, Levels and Tiers, which sets
restrictions to some of the encoder's parameters. The supported level
codes are enumerated. By specifying a certain level, the user sets
restrictions for many of the encoder parameters. The table below
represents the typical encoding values used with each level and the most
important restrictions set by each level.

The stream profile will be automatically set by the encoder based on the
tools in use. The typical default configuration is Main Profile, Main
Tier, but your configuration may vary.

.. _table_encman_hevc-levels:

.. csv-table:: HEVC Levels and their Typical Encoding Values
   :file: hevc_levels.csv

For more detailed information on the limitations and restrictions for
the profiles, refer to the relevant specifications Section
`1.3 <https://www.itu.int/ITU-T/recommendations/rec.aspx?rec=11885>`__
Standard References.

The level selection cannot be changed after the encoder is initialized.

Stream Type
-----------

The HEVC standard defines two stream formats: Network Abstraction Layer
(NAL) unit stream format and byte stream format. The NAL unit stream
format consists of plain NAL units. The byte stream format separates
each NAL unit with zero bytes and a start code prefix which makes it
easier to separate the NAL units from each other. The NAL unit stream
does not carry the size information of the NAL units so this must be
communicated some other way. The encoder is able to produce either of
these stream formats depending on the application's needs.

When the byte stream mode (VCENC_BYTE_STREAM) is selected, the data
produced by the encoder is ready to be stored or delivered as is. This
is the default stream format.

If the application requires pure NAL units (VCENC_NAL_UNIT_STREAM), the
data produced by the encoder does not contain the 4-byte start code at
the beginning of each NAL unit. Without the start code the individual
NAL units cannot be separated by simply parsing the stream. To allow the
separation, the encoder returns the size of each generated NAL unit in a
separate user-allocated buffer. This mode is most commonly used in
streaming applications.

Encoded Picture Size
--------------------

The width and height of the encoded picture in pixels must be specified
when initializing an encoder instance. Notice the difference between the
input picture size as captured by a camera and the final encoded image
size. The input image size can be different from the encoded
one if the input image is cropped before the encoding process. The main
limitations of the encoded picture size are set by the selected profile
and level. Refer to :ref:`Levels table <table_encman_hevc-levels>` for
more details. The picture size cannot be altered after the initialization.

Some implementation specific limitations are in force for H.265:

-  the encoded picture width must be a multiple of 2

-  the height must be a multiple of 2

-  the smallest encoded picture size is 130x130 and

-  The maximum encoded picture size is 4096 x 4096 pixels.

Limitations apply to the width of the input picture. Even though the
encoded picture width can be a multiple of 2, the horizontal scanline
must be a multiple of 16. This means that the memory offset from the
start of a pixel row to the beginning of the next pixel row is always a
multiple of 16. This assumption can be seen in the initial value of the
pre-processor parameters, where the input source image width is rounded
up to the next multiple of 16 of the encoded width. There is no such
limitation on the height of the picture. The figure below shows the
described limitation regarding the input picture horizontal scanline.

.. _fig_encman_pic-size:

.. figure:: /_images_src/encoded_pic_size.png
   :alt: Limitation of Input Picture Width
   :align: center

   Limitation of Input Picture Width.

Frame Rate Descriptor
---------------------

In order to be able to efficiently control the bit rate the encoder
needs a target frame rate. The frame rate is specified with a frame rate
numerator and a frame rate denominator. The division of the frame rate
numerator and frame rate denominator defines how many frames are being
encoded per second and how many bits each of the encoded frames
should use on average. The frame rate numerator also defines the stream
time scale, i.e. the number of equal sub-intervals, called ticks, within
a second. The ticks are used as a base unit for the time increment of
each individual frame. More details about the frame timing are given in
the :ref:`Stream Production <encman_stream-production>` section.

NOTE: Keep in mind that this is just a target frame rate. The time
increments of the encoded video frames will determine the real frame
rate. In other words, even if a 30 fps frame rate (i.e. frame rate
numerator 30 and denominator 1) is targeted but the time between two
consecutive frames is always 2 ticks, the final stream will have a 15
fps frame rate.

Default Encoder Initial Values
------------------------------

-  Output bit rate is set to the typical bit rate for the selected
   profile and level (Refer to :ref:`Levels table <table_encman_hevc-levels>`)

-  Picture-based rate control: disabled

-  Picture skipping: disabled

-  HRD: disabled

-  Pre-processing: disabled

-  Multi-Slices: disabled

Encoder Release
---------------

At the end of the encoding process the encoder in use must be released.
Use :ref:`VCEncRelease <vcencrelease>`\ () to perform a safe release of
all the resources allocated when the encoder was initialized. If a
release fails, the encoder instance was most likely corrupted and there
is no safe way to assure that all the encoder's resources are freed.
