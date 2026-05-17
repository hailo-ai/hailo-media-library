Configuration of the Encoder
============================

There are several encoding parameters that can be updated after the
encoder has been initialized. The configuration API call
:ref:`VCEncSetCodingCtrl <vcencsetcodingctrl>`\ () takes two parameters: an
encoder instance and the address of a specific data structure that
contains the new values. It is also possible to read the currently used
values :ref:`VCEncGetCodingCtrl <vcencgetcodingctrl>`\ (). This provides
an easy way to update just some of the values in a set of parameters
grouped together in a structure. Some of the encoder parameters can be
changed only before a stream is started and others can be altered at any
time between two-frame encoding.

Effects of Quantization
-----------------------

In order to compress the video frames the encoder performs quantization.
The size of the quantization step is controlled by adjusting the
Quantization Parameter (QP). A higher QP means a larger quantization
step. This will lower the visual quality and simultaneously raise the
compression rate. A lower QP means a smaller quantization step, which
makes the visual quality better and simultaneously lowers the
compression rate.

Bit Rate Control Configuration
------------------------------

The purpose of Rate Control (RC) is to adjust the QP so that the
resulting stream will have the required amount of bits. The rate control
is configured initially to produce the maximum bit rate allowed within
the selected stream profile and level. The bit rate and some other rate
control parameters can be updated after the initialization of the
encoder using :ref:`VCEncSetRateCtrl <VCEncSetRateCtrl>`\ (). The current
parameters can be retrieved using
:ref:`VCEncGetRateCtrl <VCEncGetRateCtrl>`\ (). The RC controls the output
bit rate by changing the quantization parameter (QP) or, in extreme
cases, by skipping entire frames from being encoded.

The rate control parameter values can be changed at any time during the
encoding sequence if the Hypothetical Reference Decoder (HRD) is
disabled. However, the rate control is reset whenever the parameters are
updated. The rate control parameters are described as follows:

-  **Picture based rate control** - a frame based rate control
   algorithm, which can be turned ON or OFF. If ON, the RC can adjust
   the QP between frames.

-  **CTB based rate control** - a Coding Tree Block (CTB) based rate
   control, which can be turned ON or OFF. If ON, the RC can adjust the
   QP inside a frame.

-  **Picture skipping** - When the output rate cannot be adjusted via
   the QP changes, picture skipping can help control it; picture
   skipping can be turned ON or OFF. Note that if HRD is enabled it may
   skip frames even if picture skipping is disabled.

-  **Default QP** - This is the default or initial QP used by the
   encoder. This will be used for the first encoded picture when the
   rate control is turned ON or for every encoded picture if the rate
   control is turned OFF. A value of (-1) lets the encoder calculate a
   typical QP value based on the resolution, frame rate and target
   bitrate.

-  **Min QP** - the minimum value QP that can be used by the encoder and
   is relevant only when the RC is turned ON.

-  **Max QP** - the maximum QP which can be set by the encoder; also in
   use only if the RC is turned ON.

-  **Output bit rate** - the target bit rate (in bits per second) for
   the output stream and it is used when one or more of the following is
   turned ON: picture based RC, picture skipping or HRD. It is initially
   set to the maximum value defined by the profile and level of the
   stream but it can be changed if needed.

-  **HRD -** an algorithm for checking a bit stream with its bit rate to
   verify that the amount of input stream buffer memory required in a
   decoder is less than the standard-defined buffer size. This can be
   turned ON or OFF, but by turning it OFF the output stream might not
   be 100% standard compatible.

-  **HRD CPB size** - the size of the Coded Picture Buffer (CPB) used in
   the HRD model. By default, it is set to the maximum size allowed for
   the initialized encoder level. Do not change this parameter if you
   are unfamiliar with this feature.

-  **GOP length** - GOP is a group of successive pictures within a video
   stream. GOP usually contains an Intra frame (I-frame) and several
   Inter frames (Predicted frame). Intra frames make video easier
   to search and edit, but they use more bits which decreases the
   overall quality of the stream. Rate control uses the GOP length to
   match the average bit rate of each GOP to the target bit rate. Thus
   the GOP length setting affects how fast the rate control reacts to
   changes in the video sequence and encoded bits per frame. E.g., a GOP
   length of 30 frames on a 30 fps stream will match the target bitrate
   every second, whereas a GOP length of 150 frames will match the
   target bitrate over five seconds. Note that it is up to the
   application to choose whether to insert an I-frame at the beginning
   of each GOP or not. i.e. every I-frame starts a new GOP but every GOP
   doesn't need to start with an I-frame.

-  **Intra QP delta** - The intra frames in HEVC video can sometimes
   introduce noticeable flickering because of different prediction
   methods. This problem can be overcome by adjusting the quantization
   of the intra frames compared to the surrounding inter frames.

Typically it is sufficient to just set the target bit rate. This can be
handled by calling :ref:`VCEncGetRateCtrl <VCEncGetRateCtrl>`\ () to get
the original values, changing the target bit rate and setting the values
by :ref:`VCEncSetRateCtrl <VCEncSetRateCtrl>`\ ().

To achieve accurate control of the output bit rate, the CTB based rate
control is enabled during initialization. If QP does not change in the
middle of a picture, then the CTB should be disabled.

In cases where the target bit rate is too low, the encoder may not be
able to reach the target no matter what QP is used. Therefore it may be
useful to enable picture skipping for very low bit rates. If the output
frame rate is not required to be the same as the input frame rate, this
parameter may be enabled to give the rate control algorithm more freedom
to allocate bits for pictures.

Setting minimum and/or maximum QP can be used to restrict the rate
control to only use QP values in the range [Min QP, Max QP]. This is
feasible in a Variable Bitrate (VBR) operation where constant quality is
preferred over the constant bitrate. On the other hand, these
limitations also limit the rate control's possibilities to achieve the
target bitrate.

The HRD feature of the encoder is disabled by default. The encoder will
not be able to guarantee that restrictions placed on the standard
[#f_hevc_spec]_ will always apply. When the HRD is enabled,
the encoder runs a model of the decoder input stream buffer (coded
picture buffer) to make sure that at the decoding time of a certain
picture, the buffer contains enough data so that the decoder may decode
the picture. On the other hand, the HRD model assures that the buffer
can accommodate the stream data at any point of time. The model assumes
that there is a constant bit rate channel between the encoder and the
decoder. The size of the coded picture buffer is specified in the
standard [#f_hevc_spec]_ for each level. When the HRD is disabled,
the operation of the rate control is alleviated so that the encoder does
not have to keep the channel occupied all the time and the encoder may
produce images whose size exceeds the buffer occupancy at the decoding
time of the picture.

NOTE: When HRD is enabled, the selected bitrate and any custom CPB size
must be within the limits set by the initialized encoder level (Refer to
values in the HEVC standard [#f_hevc_spec]_ Once the stream has
been started, the rate control parameters (bitrate) cannot be altered
anymore. This is due to the fact that the stream start headers contain
information about the selected bitrate and CPB size.

VBR and CBR Video
-----------------

**Variable Bit Rate** (**VBR**) is the preferred choice for locally
stored video. The visual quality stays constant, more bits are allocated
for complex sections and fewer bits are allocated for simple sections.
VBR has better quality versus space ratio than CBR video. The simplest
solution is to use a constant QP for the entire video. In this encoder,
VBR can be constrained so that rate control tries to reach the target
bit rate over the time. Refer to the HEVC standard [#f_hevc_spec]_
for maximum bit rate limits.

**Constant Bit Rate** (**CBR**) is useful for real-time streaming video
over a constant bandwidth channel. CBR is not a good choice for storage,
since it doesn't allocate enough bits for complex sections and wastes
bits on simple sections. Perfectly constant bit rate is close to
impossible to achieve with a real-time one-pass encoder such as this
encoder. The best way for a one-pass encoder to achieve CBR is to change
the QP of the CTB inside the frame during encoding (CTB-based RC can
partially implement this function). This might have the adverse effect
of the lower part of the frame becoming blurry.

The figure below illustrates the trade-off between visual quality
variance and bitrate variance and how it is mapped to three typical
video use cases. The table below shows recommended settings for rate
control parameters for each of these example use cases. Keep in mind
that the QP limits are heavily dependent on the target bit rate and
frame rate.

.. _fig_encman_vbr-vs-cbr:

.. figure:: /_images_src/fig_encman_vbr-vs-cbr.png
   :alt: Variable Bit Rate vs. Constant Bit Rate
   :align: center

   Variable Bit Rate vs. Constant Bit Rate

.. _table_encman_recommended_rate_control_settings:

.. csv-table:: Recommended Rate Control Settings
   :file: table_encman_recommended_rate_control_settings.csv

Coding Control Settings
-----------------------

There are several coding control parameters that can be altered using
:ref:`VCEncSetCodingCtrl <vcencsetcodingctrl>`\ (). Current settings can
be retrieved using :ref:`VCEncGetCodingCtrl <vcencgetcodingctrl>`\ (). The
recommended way of changing any of the coding control parameters is to
first retrieve the current settings and then change the desired
parameters. All the parameters with the exception of the “slice size”
can only be set before starting the encoding.

Available options:

-  **Slice size** - Each encoded picture can be divided into several
   slices. Slices help in the error recovery of the erroneous streams
   and their role is to split the picture data into smaller independent
   packages. The size is specified in Coding Tree Unit (CTU) rows. This
   value can be altered at any time during the encoding process.

-  **SEI messages** - When enabled, a Supplemental Enhancement
   Information (SEI) message containing picture timing information will
   be inserted into the stream before every encoded frame. Buffering
   period information will be inserted into the stream before every
   encoded Instantaneous Decoder Refresh (IDR) frame when Hypothetical
   Reference Decoder (HRD) is enabled. If not using these SEI messages,
   the stream doesn't contain any timing information so it must be
   communicated some other way.

-  **Video full range** - Defines the input YCbCr data sample range that
   will be included in the stream headers. If not set correctly, the
   video range dynamic might suffer degradation on the decoder side.

-  **Disable de-blocking filter** - Disables the de-blocking filter,
   thus lowering the encoding and decoding complexity but also lowering
   the video quality.

-  **Disable SAO filter** - Disables the Sample Adaptive Offset (SAO)
   filter, thus lowering the encoding and decoding complexity but also
   lowering the video quality.

-  **Sample aspect ratio width and height** - Defines the aspect ratio
   of the input picture samples that will be included in the stream
   headers. The default aspect ratio is square.

-  **Field order** - When interlaced viewMode is used, the field order
   can either be bottom-first or top-first. Some applications may assume
   a certain field order. The default value of 0 specifies a field order
   of bottom-first.

-  **ROI encoding -** In Region-of-Interest (ROI) encoding, two
   rectangular areas can be set to have higher encoding quality.

-  **QP map based ROI -** QP delta for each CU can be defined through
   the QP map stored in DRAM. This enables more flexible, arbitrary
   shape ROI areas.

-  **Denoise filtering** **-** Allows filtering of the noise in the
   input frame caused by for example, low light conditions in the camera
   sensor. This improves both the input frame quality and the
   compression rate.

-  **GDR** **-** Instead of I-frames, Gradual Decoder Refresh (GDR) uses
   I-slices that gradually cover the whole frame area. This enables
   faster frame loss recovery and spreads the heavier computational load
   of I-frames over several frames.

Example:
~~~~~~~~

.. code-block:: c

   VCEncInst encoder;
   VCEncCodingCtrl codingCfg;

   /* encoder is already initialized and we have an instance */
   if(VCEncGetCodingCtrl(encoder, &codingCfg) != VCENC_OK)
   {
      /* handle error */
   }
   else
   {
      codingCfg.sliceSize = 5; /* 5 CTU rows in each slice */
      if(VCEncSetCodingCtrl(encoder, &codingCfg) != VCENC_OK)
      {
         /* handle error */
      }
   }

After the encoding has been started, from the above parameters, only the
slice size can be altered.

.. [#f_hevc_spec] `ITU-T H265 <https://www.itu.int/ITU-T/recommendations/rec.aspx?rec=11885>`__
