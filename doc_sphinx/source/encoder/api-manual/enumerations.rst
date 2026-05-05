Enumerations Used in the API
============================

.. _VCEncColorConversion:

VCEncColorConversion Enumeration
--------------------------------

Specifies the RGB to YUV conversion type to be used. Used in the
VCEncPreProcessingCfg data structure.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncColorConversion Values
     - | Description
   * - | VCENC_RGBTOYUV_BT601
     - | Conversion according to ITU-R Recommendation `ITU-R-REC-BT.601 <https://www.itu.int/rec/R-REC-BT.601/>`_
   * - | VCENC_RGBTOYUV_BT709
     - | Conversion according to ITU-R Recommendation `ITU-R-REC-BT.709 <https://www.itu.int/rec/R-REC-BT.709/en>`_
   * - | VCENC_RGBTOYUV_USER_DEFINED
     - | Conversion using user-defined coefficients coeffA, coeffB, coeffC, coeffE and coeffF in formula:
       | Y = :math:`\frac{(\ coeffA\ *\ R\ + \ coeffB | \ *\ G\ + \ coeffC\ *\ B\ )}{65536}`
       | Cb = :math:`\frac{\left( coeffE*(\ B - Y\ ) \right)}{65536 + 128}`
       | Cr = :math:`\frac{\left( coeffF*(R - Y) \right)}{65536 + 128}`

.. _VCEncProfile:

VCEncProfile Enumeration
------------------------

Specifies the HEVC(H.265)/AVC(H.264) profile of the generated stream.
Used in the `VCEncConfig <#VCEncConfig>`__ structure.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncProfile Values
     - | Description
   * - VCENC_HEVC_MAIN_PROFILE
     - Allows for a bit depth of 8 bits per sample with 4:2:0 chroma sampling.
   * - VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE
     - Allows for a single still picture to be encoded with the same constraints as the Main profile.
   * - VCENC_HEVC_MAIN_10_PROFILE
     - Allows for a bit depth of 8-bits to 10-bits per sample with 4:2:0 chroma sampling.
   * - VCENC_H264_MAIN_PROFILE
     - Used for standard-definition digital TV broadcasts that use the MPEG-4 format.
   * - VCENC_H264_HIGH_PROFILE
     - The primary profile for broadcast and disc storage applications, particularly for high-definition TV applications.
   * - VCENC_H264_HIGH_10_PROFILE
     - This profile builds on top of the High Profile, adding support for up to 10 bits per sample.

.. _VCEncLevel:

VCEncLevel Enumeration
----------------------

Specifies the HEVC/H.264 level of the generated stream. Used in the
`VCEncConfig <#VCEncConfig>`__ structure.

.. list-table:: HEVC/AVC Levels
   :widths: 5 3 2 3 3 3
   :header-rows: 1

   * - | VCEncLevel Values
     - | Encoded Picture Size
     - | Frame Rate (fps)
     - | Max Luma Picture Size
       | (samples)
     - | Max Luma Sample Rate
       | (samples/sec)
     - | Max Bit Rate (Kbps)
   * - HEVC (H265)
     -
     -
     -
     -
     -
   * - VCENC_HEVC_LEVEL_1
     - QCIF
     - 15
     - 36,864
     - 552,960
     - 128
   * - VCENC_HEVC_LEVEL_2
     - CIF
     - 30
     - 122,880
     - 3,686,400
     - 1500
   * - VCENC_HEVC_LEVEL_2_1
     - Q720p
     - 30
     - 245,760
     - 7,372,800
     - 3000
   * - VCENC_HEVC_LEVEL_3
     - QHD
     - 30
     - 552,960
     - 16,588,800
     - 6000
   * - VCENC_HEVC_LEVEL_3_1
     - 1280x720
     - 30
     - 983,040
     - 33,177,600
     - 10000
   * - VCENC_HEVC_LEVEL_4
     - 2Kx1080
     - 30
     - 2,228,224
     - 66,846,720
     - 12000
   * - VCENC_HEVC_LEVEL_4_1
     - 2Kx1080
     - 60
     - 2,228,224
     - 133,693,440
     - 20000
   * - VCENC_HEVC_LEVEL_5
     - 4096x2160
     - 30
     - 8,912,896
     - 267,386,880
     - 25000
   * - VCENC_HEVC_LEVEL_5_1
     - 4096x2160
     - 60
     - 8,912,896
     - 534,773,760
     - 40000
   * - VCENC_HEVC_LEVEL_5_2
     - 4096x2160
     - 120
     - 8,912,896
     - 1,069,547,520
     - 60000
   * - VCENC_HEVC_LEVEL_6
     - 8192x4320
     - 30
     - 35,651,584
     - 1,069,547,520
     - 60000
   * - VCENC_HEVC_LEVEL_6_1
     - 8192x4320
     - 60
     - 35,651,584
     - 2,139,095,040
     - 120000
   * - VCENC_HEVC_LEVEL_6_2
     - 8192x4320
     - 120
     - 35,651,584
     - 4,278,190,080
     - 240000
   * - AVC (H264)
     -
     -
     -
     -
     -
   * - VCENC_H264_LEVEL_1
     - Sub-QCIF
     - 15
     - 99 (QCIF)
     - 1,485
     - 64
   * - VCENC_H264_LEVEL_1
     - QCIF
     - 15
     - 99 (QCIF)
     - 1,485
     - 64
   * - VCENC_H264_LEVEL_1_b
     - Sub-QCIF
     - 15
     - 99 (QCIF)
     - 1,485
     - 128
   * - VCENC_H264_LEVEL_1_b
     - QCIF
     - 15
     - 99 (QCIF)
     - 1,485
     - 128
   * - VCENC_H264_LEVEL_1_1
     - QCIF
     - 30
     - 396 (QCIF)
     - 3000
     - 192
   * - VCENC_H264_LEVEL_1_1
     - QVGA
     - 10
     - 396 (QCIF)
     - 3000
     - 192
   * - VCENC_H264_LEVEL_1_2
     - QVGA
     - 20
     - 396 (QCIF)
     - 6000
     - 384
   * - VCENC_H264_LEVEL_1_2
     - CIF
     - 15
     - 396 (QCIF)
     - 6000
     - 384
   * - VCENC_H264_LEVEL_1_3
     - QVGA
     - 30
     - 396 (QCIF)
     - 11,880
     - 768
   * - VCENC_H264_LEVEL_1_3
     - CIF
     - 30
     - 396 (QCIF)
     - 11,880
     - 768
   * - VCENC_H264_LEVEL_2
     - CIF
     - 30
     - 396 (QCIF)
     - 11,880
     - 2000
   * - VCENC_H264_LEVEL_2_1
     - 512x384
     - 25
     - 792
     - 19,800
     - 4000
   * - VCENC_H264_LEVEL_2_2
     - VGA
     - 15
     - 1,620 (PAL)
     - 20,250
     - 4,000
   * - VCENC_H264_LEVEL_2_2
     - 720x480
     - 15
     - 1,620 (PAL)
     - 20,250
     - 4,000
   * - VCENC_H264_LEVEL_3
     - VGA
     - 30
     - 1,620 (PAL)
     - 40,500
     - 10,000
   * - VCENC_H264_LEVEL_3
     - 720x576
     - 25
     - 1,620 (PAL)
     - 40,500
     - 10,000
   * - VCENC_H264_LEVEL_3
     - 720x480
     - 30
     - 1,620 (PAL)
     - 40,500
     - 10,000
   * - VCENC_H264_LEVEL_3_1
     - 1280x720
     - 30
     - 3,600 (HD 720p)
     - 108,000
     - 14,000
   * - VCENC_H264_LEVEL_3_2
     - 1280x1024
     - 30
     - 5,120 (SXGA)
     - 216,000
     - 20,000
   * - VCENC_H264_LEVEL_4
     - 1920x1088
     - 30
     - 8,192 (1080p)
     - 245,760
     - 20,000
   * - VCENC_H264_LEVEL_4_1
     - 1920x1088
     - 30
     - 8,192 (1080p)
     - 245,760
     - 50,000
   * - VCENC_H264_LEVEL_4_2
     - 1920x1088
     - 60
     - 8,192 (1080p)
     - 491,520
     - 50,000
   * - VCENC_H264_LEVEL_5
     - 2048x1088
     - 60
     - 22,080
     - 589,824
     - 135,000
   * - VCENC_H264_LEVEL_5_1
     - 3840x2160
     - 15
     - 36,864
     - 983,040
     - 240,000

.. _VCEncPictureCodingType:

VCEncPictureCodingType Enumeration
----------------------------------

Specifies the encoding type for the specified picture. Used in the
`VCEncIn <#VCEncIn>`__, VCEncOut and
`VCEncGopPicConfig <#VCEncGopPicConfig>`__ data structures.

.. csv-table::
   :header: "VCEncPictureCodingType Values", "Description"

    "VCENC_INTRA_FRAME", "The picture should be INTRA coded."
    "VCENC_PREDICTED_FRAME", "The picture should be INTER coded using the previous picture as a predictor."
    "VCENC_BIDIR_PREDICTED_FRAME", "The picture should be INTER coded using the backward and/or forward pictures as predictors."

.. _VCEncPictureRotation:

VCEncPictureRotation Enumeration
--------------------------------

Specifies the YUV picture rotation before encoding. Used in the
VCEncPreProcessingCfg data structure.

.. csv-table::
   :header: "VCEncPictureRotation Values", "Description"

    "VCENC_ROTATE_0", "No rotation."
    "VCENC_ROTATE_90R", "Rotates the picture clockwise by 90 degrees before the encoding."
    "VCENC_ROTATE_90L", "Rotates the picture counter-clockwise by 90 degrees before the encoding."
    "VCENC_ROTATE_180R", "Rotates the picture clockwise by 180 degrees before the encoding."

.. _VCEncPictureType:

VCEncPictureType Enumeration
----------------------------

Specifies the input picture format. Used in the VCEncPreProcessingCfg
data structure.

The formats are described in the `Video Frame Storage
Format <#GUIDE_VideoFrameStorageFormat>`__ section.

.. csv-table::
   :header: "VCEncPictureType Values", "Description"

    "VCENC_YUV420_PLANAR", ""
    "VCENC_YUV420_SEMIPLANAR", ""
    "VCENC_YUV420_SEMIPLANAR_VU", ""
    "VCENC_YUV422_INTERLEAVED_YUYV", ""
    "VCENC_YUV422_INTERLEAVED_UYVY", ""
    "VCENC_RGB565", ""
    "VCENC_BGR565", ""
    "VCENC_RGB555", ""
    "VCENC_BGR555", ""
    "VCENC_RGB444", ""
    "VCENC_BGR444", ""
    "VCENC_RGB888", ""
    "VCENC_BGR888", ""
    "VCENC_RGB101010", ""
    "VCENC_BGR101010", ""
    "VCENC_YUV420_PLANAR_10BIT_I010", ""
    "VCENC_YUV420_PLANAR_10BIT_P010", ""
    "VCENC_YUV420_PLANAR_10BIT_PACKED_PLANAR", ""
    "VCENC_YUV420_10BIT_PACKED_Y0L2", ""

.. _VCEncStreamType:

VCEncStreamType Enumeration
---------------------------

Specifies the type of the stream generated. Used in the
`VCEncConfig <#VCEncConfig>`__ structure.

.. csv-table::
   :header: "VCEncStreamType Values", "Description"

    "VCENC_BYTE_STREAM", "The encoder will produce the stream in byte stream format."
    "VCENC_NAL_UNIT_STREAM", "The encoder will produce the stream in NAL units."
