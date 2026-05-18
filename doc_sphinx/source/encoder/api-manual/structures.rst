Structures
==========

.. _VCEncApiVersion:

VCEncApiVersion
---------------

A structure containing the API's major and minor version number.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncApiVersion Members
     - | Type
     - | Description
   * - | major
     - | u32
     - | The major version number.
   * - | minor
     - | u32
     - | The minor version number.

.. _VCEncBuild:

VCEncBuild
----------

A structure containing the encoder's build information.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncBuild Members
     - | Type
     - | Description
   * - | swBuild
     - | u32
     - | The internal release version of the encoder software.
   * - | hwBuild
     - | u32
     - | The internal release version of the encoder hardware.

.. _VCEncCodingCtrl:

VCEncCodingCtrl
---------------

A structure containing the encoder's coding parameters.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncCodingCtrl Members
     - | Type
     - | Description
   * - | sliceSize
     - | u32
     - | Sets the size of a slice in CTB rows. A zero value disables the use of slices.
       | This parameter can be updated during the encoding process, between any picture encoding.
       | Valid value range: [0, height/ctu_size]
       | Default value is 0.
   * - | seiMessages
     - | u32
     - | Enables insertion of picture timing and buffering period SEI messages into the stream
       | in the beginning of every encoded frame.
       | Valid value range: [0, 1]
       | Default value is 0 (disabled).
   * - | vuivideoFullRange
     - | u32
     - | Input video signal sample range.
       | Valid value range: [0, 1]
       | 0 = Y range in [16, 235], Cb&Cr range in [16, 240]
       | 1 = Y, Cb and Cr range in [0, 255]
       | Default value is 1
   * - | disableDeblockingFilter
     - | u32
     - | Value of disable_deblocking_filter_idc. Valid value range: [0, 1]
       | 0 = Inloop deblocking filter enabled (best quality).
       | 1 = Inloop deblocking filter disabled
       | Default value is 0.
   * - | tc_Offset
     - | i32
     - | Deblocking filter TC offset.
       | Valid value range: [-6, 6]
       | Default value is -2.
   * - | beta_Offset
     - | i32
     - | Deblocking filter beta offset.
       | Valid value range: [-6, 6]
       | Default value is 5.
   * - | enableDeblockOverride
     - | u32
     - | Deblocking override enable flag.
       | Valid value range: [0, 1]
       | 0 = Disable override
       | 1 = enable override
       | Default value is 0.
       | (Invalid for H.264; instead, use deblockOverride in slice header)
   * - | deblockOverride
     - | u32
     - | Deblocking override flag.
       | Valid value range: [0, 1]
       | 0 = do not override filter parameter
       | 1 = override filter parameter.
       | Default value is 0.
   * - | enableSao
     - | u32
     - | Disable or enable SAO Filter.
       | Valid value range: [0, 1]
       | 0 = SAO disabled
       | 1 = SAO enabled.
       | Default value is 1.
       | (Invalid for H.264)
   * - | enableScalingList
     - | u32
     - | Specifies whether to use average or default scaling list.
       | Valid value range: [0, 1]
       | 0 = average scaling list.
       | 1 = default scaling list
       | Default value is 0.
   * - | sampleAspectRatioWidth
     - | u32
     - | Horizontal size of the sample aspect ratio (in arbitrary units), 0 for unspecified.
       | Valid value range: [0, 65535]
   * - | sampleAspectRatioHeight
     - | u32
     - | Vertical size of the sample aspect ratio (in arbitrary units), 0 for unspecified.
       | Valid value range: [0, 65535]
   * - | enableCabac
     - | u32
     - | [0,1] H.264 entropy coding mode: 1 for CAVLC, 1 for CABAC
   * - | cabacInitFlag
     - | u32
     - | Context Adaptive Binary Arithmetic Coding (CABAC) table initial flag.
       | The cabac_init_flag will be 0 regardless of the SW setting.
       | Valid value range: [0, 1]
       | (This feature is currently not supported by the hardware.)
   * - | cirStart
     - | u32
     - | Sets the first CTB for Cyclic Intra Refresh (CIR).
       | CIR forces some of the CTBs to be intra coded and can be used to improve stream error recovery.
       | Valid value range: [0, ctbPerFrame-1].
       | Default value is 0.
   * - | cirInterval
     - | u32
     - | Sets the CTB interval for Cyclic Intra Refresh.
       | A zero value disables CIR.
       | CIR forces CTBs with numbers: cirStart, cirStart+cirInterval, cirStart+2*cirInterval, ... to be intra coded.
       | Valid value range: [0, ctbPerFrame].
       | Default value is 0 (CIR disabled).
   * - | pcm_enabled_flag
     - | i32
     - |
   * - | pcm_sample_bit_depth_luma_minus1
     - | i32
     - |
   * - | pcm_sample_bit_depth_chroa_minus1
     - | i32
     - |
   * - | pcm_loop_filter_disabled_flag
     - | i32
     - |
   * - | intraArea
     - | VCEncPictureArea
     - | Specifies a rectangular area of a Coding Tree Block (CTB) to be forced as intra coded.
       | All CTBs inside the area and including the coordinates will be intra coded.
       | Default value: disabled (intraArea.enable=0).
   * - | ipcm1Area
     - | VCEncPictureArea
     - | First area for forcing IPCM macroblocks
   * - | ipcm2Area
     - | VCEncPictureArea
     - | Second area for forcing IPCM macroblocks
   * - | roi1Area
     - | VCEncPictureArea
     - | Specifies a rectangular area of CTBs to be used as a Region-Of-Interest (ROI) with
       | modified Quantization Parameter (QP) value. All CTBs inside the area and including
       | the coordinates will be coded using the roi1DeltaQp value to modify the default picture QP.
       | Default value: disabled (intraArea.enable=0).
   * - | roi2Area
     - | VCEncPictureArea
     - | Specifies a rectangular area of CTBs to be used as a second Region-Of-Interest with
       | modified QP value. All CTBs inside the area and including the coordinates will be
       | coded using the roi2DeltaQp value to modify the default picture QP.
       | Default value: disabled (intraArea.enable=0).
   * - | roi1DeltaQp
     - | i32
     - | Specifies the QP delta value for the first ROI area.
       | The QP to be used will be: qpHdr + roi1DeltaQp.
       | Valid value range: [-15, 0]
       | Default value is 0 (disabled).
   * - | roi2DeltaQp
     - | i32
     - | Specifies the QP delta value for the second ROI area.
       | The QP to be used will be: qpHdr + roi2DeltaQp.
       | Valid value range: [-15, 0]
       | Default value is 0 (disabled).
   * - | fieldOrder
     - | u32
     - | Specifies the field order for interlaced coding.
       | 0 = bottom field first
       | 1 = top field first
       | Valid value range: [0, 1].
       | Default value: 0.
   * - | chroma_qp_offset
     - | i32
     - | Chroma QP offset.
       | Valid value range: [-12, 12].
       | Default value: 0.
   * - | roiMapDeltaQpEnable
     - | u32
     - | Enable ROI map function.
       | 0 = disable,
       | 1 = enable
       | Valid value range: [0, 1].
       | Default value: 0.
   * - | roiMapDeltaQpBlockUnit
     - | u32
     - | Specifies ROI map block unit size.
       | 0 = 64x64
       | 1 = 32x32
       | 2 = 16x16
       | 3 = 8x8
       | Valid value range for HEVC: [0, 3].
       | Valid value range for H.264: [0, 2].
       | Default value: 0.
   * - | noiseReductionEnable
     - | u32
     - | Obsolete, not supported.
       | Default value: 0.
   * - | noiseLow
     - | u32
     - | Specifies the low boundary for noise estimation.
       | Valid value range: [1, 30].
       | Default value: 10.
   * - | firstFrameSigma
     - | u32
     - | Specifies the initial noise sigma.
       | Valid value range: [1, 30].
       | Default value: 11.
   * - | gdrDuration
     - | u32
     - | Gradual Decoding Refresh (GDR) duration.
       | A value of 0 means that GDR feature is disabled. Nonzero values define the amount of
       | pictures (frames not fields) that it will take to do GDR.
       | The starting point for GDR is the frame which has codingType set to VCENC_INTRA_FRAME.
       | However, this frame will be encoded as VCENC_PREDICTED_FRAME.
       | intraArea and roi1Area are used to implement the GDR function, therefore these features
       | cannot be used if GDR is enabled.
       | GDR function is not supported with B-frames.
       | Valid value range: [0, 0xFFFFFFFF].
   * - | codecH264
     - | i32
     - | Enable H.264 encoding.
       | 0 = HEVC
       | 1 = H.264
   * - | inputLineBufEn
     - | u32
     - | For low latency: enable input image control signals
   * - | inputLineBufLoopBackEn
     - | u32
     - | For low latency: input buffer loopback mode enable
   * - | inputLineBufDepth
     - | u32
     - | For low latency: input buffer depth in MB lines
   * - | inputLineBufHwModeEn
     - | u32
     - | For low latency: hardware handshake
   * - | inputLineBufCbFunc
     - | EncInputLineBufCallbackFunc
     - | For low latency: callback function
   * - | inputLineBufCbData
     - | void *
     - | For low latency: callback function data
   * - | vuiColorDescription
     - | VuiColorDescription
     - | The color description in the VUI.
   * - | vuiVideoSignalTypePresentFlag
     - | U32
     - | Whether to present the video signal type in the VUI flag.
       | 0 default: do not present
       | 1 present
   * - | vuiVideoFormat
     - | U32
     - | The video format in the VUI.
       | 0: component
       | 1: PAL
       | 2: NTSC
       | 3: SECAM
       | 4: MAC
       | 5 (default): UNDEF

.. _VCEncConfig:

VCEncConfig
-----------

A structure containing the encoder's initial configuration parameters.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncConfig Members
     - | Type
     - | Description
   * - | streamType
     - | VCEncStreamType
     - | The type of the stream generated.
   * - | profile
     - | VCEncProfile
     - | The HEVC/H.264 profile of the generated stream.
   * - | level
     - | VCEncLevel
     - | The HEVC/H.264 level of the generated stream.
       | For more information refer to the Initialization and release of the encoder section.
   * - | width
     - | u32
     - | The width of the encoded image in pixels.
       | Horizontal stride is assumed to be the next multiple of 16 larger than or equal to this value.
       | Refer to limitations listed in the Encoder Limitations section.
       | Valid value range: [130, 4096]
   * - | height
     - | u32
     - | The height of the encoded image in pixels.
       | Refer to limitations listed in the Encoder Limitations section.
       | Valid value range: [130, 8192]
   * - | frameRateNum
     - | u32
     - | The numerator part of the input frame rate.
       | The frame rate is defined by the frameRateNum/frameRateDenom ratio. This value also
       | defines the time resolution as ticks per second.
       | Valid value range: [1, 1048575]
   * - | frameRateDenom
     - | u32
     - | The denominator part of the input frame rate.
       | This value must be equal or less than the numerator part frameRateNum.
       | Valid value range: [1, 65535]
   * - | refFrameAmount
     - | u32
     - | Specifies the amount of reference frame buffers that will be allocated.
       | If only I frames are encoded, 0 should be set.
       | If Group Of Pictures (GOP) size is 1, only previous frame or field can be used as reference.
       | 1 should be set for progressive frames or 2 should be set for interlaced fields.
       | If GOP size is 2 or 3,
       | 2 should be set.
       | If GOP size is 4, 5, 6 or 7,
       | 3 should be set.
       | If GOP size is 8,
       | 4 should be set.
       | Valid value range: [0, 4]
   * - | strongIntraSmoothing
     - | u32
     - | Valid value range: [0,1]
       | (Currently only a value of 0 is supported).
       | Value 0 means that the bi-linear interpolation is not used in the Coded Video Sequence (CVS).
       | Value 1 means that bi-linear interpolation is conditionally used in the filtering process
       | in the CVS as specified in clause 8.4.4.2.3 of the H.265 specification[1].
       | strong_intra_smoothing_enabled_flag.
       | (Invalid for H.264)
   * - | compressor
     - | u32
     - | Enable/disable embedded reference frame compression:
       | Valid value range: [0,3]
       | 0 = Disable compression
       | 1 = Only enable luma compression
       | 2 = Only enable chroma compression
       | 3 = Enable both luma and chroma compression
   * - | interlacedFrame
     - | u32
     - | Valid value range: [0,1]
       | 0 = Input progressive field
       | 1 = Input frame with fields interlaced [0]
       | (H.264 not supported yet)
   * - | bitDepthLuma
     - | u32
     - | Specifies the bit depth of the samples of the luma array in the encoded stream.
       | Valid value range: [8,10]
       | 8 = 8 bits
       | 9 = 9 bits
       | 10 = 10 bits
   * - | bitDepthChroma
     - | u32
     - | Specifies the bit depth of the samples of the chroma array in the encoded stream.
       | Valid value range: [8,10]
       | 8 = 8 bits
       | 9 = 9 bits
       | 10 = 10 bits
   * - | enableOutputCuInfo
     - | u32
     - | Enable/disable CU Information dumping.
       | Valid value range: [0,1]
       | 0 = Disable CU information dumping
       | 1 = Enable CU information dumping
   * - | maxTLayers
     - | u32
     - | Specifies the maximum number of temporal layers.
       | Valid value range: [1,5]
       | Default: 1
       | (H.264 support planned)
   * - | codecH264
     - | i32
     - | Enable H.264 encoding.
       | 0 = HEVC
       | 1 = H.264
   * - | codecDummy
     - | i32
     - | Enable standalone processing

.. _VCEncGopConfig:

VCEncGopConfig
--------------

A structure which contains all GOP structures that will be used
throughout the sequence.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncGopConfig Members
     - | Type
     - | Description
   * - | pGopPicCfg
     - | VCEncGopPicConfig
     - | Pointer to an array of structure VCEncGopPicConfig. Each VCEncGopPicConfig structure
       | contains the Group of Pictures (GOP) configuration of one picture.
   * - | size
     - | u8
     - | Number of GOP structures VCEncGopPicConfig pointed to by pGopPicCfg.
   * - | id
     - | u8
     - | Index of structure VCEncGopPicConfig used by the current picture, ranging from 0 to size-1.
   * - | id_next
     - | u8
     - | Index of structure VCEncGopPicConfig used by the next picture, ranging from 0 to size-1.
       | (H.264 only, used for MMO)
   * - | delta_poc_to_next
     - | i32
     - | Picture Order Count (POC) difference between the next picture and the current picture.
       | (H.264 only, used for MMO)
   * - | ltrInterval
     - | i32
     - | Long term reference picture interval.
       | 0:  disable
       | > 0: gap between 2 long term reference pictures in display order

.. _VCEncHEVCCuOutData:

VCEncHEVCCuOutData [HEVC only]
------------------------------

A structure which contains the Coding Unit (CU) information of a picture
output by hardware.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncHEVCCuOutData Members
     - | Type
     - | Description
   * - | ctuOffset
     - | u32
     - | Pointer to the memory containing the total Coding Unit (CU) number by the end of
       | each Coding Tree Unit (CTU).
   * - | cuData
     - | u8
     - | Pointer to the CU information of a picture.

.. _VCEncHEVCCuInfo:

VCEncHEVCCuInfo [HEVC only]
---------------------------

A structure that contains the parsed Coding Unit (CU) information.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncHEVCCuInfo Members
     - | Type
     - | Description
   * - | cuLocationX
     - | u8
     - | CU x-coordinate relative to Coding Tree Unit (CTU).
   * - | cuLocationY
     - | u8
     - | CU y-coordinate relative to CTU.
   * - | cuSize
     - | u8
     - | CU size:  8/16/32/64
   * - | cuMode
     - | u8
     - | CU mode:
       | 0:INTER
       | 1:INTRA
   * - | cuSadCost
     - | u32
     - | CU Sum of Absolute Difference (SAD) cost
   * - | interPredIdc
     - | u8
     - | Prediction direction - only for INTER CU.
       | 0: by list0
       | 1: by list1
       | 2: bi-direction
   * - | mv[2]
     - | VCEncHEVCMv
     - | Motion information - only for INTER CU.
       | mv[0] for list0 if it's valid
       | mv[1] for list1 if it's valid
   * - | intraPartMode
     - | u8
     - | Partition mode - only for INTRA CU.
       | 0:2Nx2N
       | 1: NxN
   * - | intraPredMode[4]
     - | u8
     - | Prediction mode - only for INTRA CU.
       | 0: planar
       | 1: DC
       | 2-34: Angular in HEVC spec
       | intraPredMode[1~3] only valid for NxN

.. _VCEncHEVCMv:

VCEncHEVCMv [HEVC only]
-----------------------

A structure that contains the motion information for the INTER Coding
Unit.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncHEVCMv Members
     - | Type
     - | Description
   * - | refIdx
     - | u8
     - | Reference index in reference list.
   * - | mvX
     - | i16
     - | Horizontal motion in 1/4 pixel.
   * - | mvY
     - | i16
     - | Vertical motion in 1/4 pixel.

.. _VCEncIn:

VCEncIn
-------

This data structure is common for all the API calls which generate a
stream. Not all fields are used when starting a stream.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncIn Members
     - | Type
     - | Description
   * - | busLuma
     - | u32
     - | Bus address of the buffer where the input picture's luminance component for
       | YCbCr420_PLANAR and YCbCr420_SEMIPLANAR formats or the whole input picture for
       | YCbYCr422_INTERLEAVED or RGB formats is located.
   * - | busChromaU
     - | u32
     - | Bus address of the buffer where the input picture's first chrominance component
       | (for YCbCr420_PLANAR) or both chrominance components (for YCbCr420_SEMIPLANAR) is located.
   * - | busChromaV
     - | u32
     - | Bus address of the buffer where the input picture's second chrominance component
       | (for YCbCr420_PLANAR) is located.
   * - | timeIncrement
     - | u32
     - | Time stamp of the frame relative to the last encoded frame. This is given in
       | ticks (one tick equals 1/frameRateNum seconds). A zero value is usually used
       | for the very first frame. The ticks per second were set at the encoder
       | initialization phase (frameRateNum).
       | For correct rate control functioning it is important to set this value appropriately.
       | The typical value for a fixed frame rate video is: timeIncrement=frameRateDenom.
   * - | pOutBuf
     - | u32
     - | Pointer to the output buffer which will be used to store the generated stream.
       | Must be a linear, memory residing buffer and 8-byte aligned.
   * - | busOutBuf
     - | u32
     - | Bus address of the output buffer, required by the hardware operations.
   * - | outBufSize
     - | u32
     - | Size of the stream buffer described above in bytes. Minimum value is 64.
   * - | codingType
     - | VCEncPictureCodingType
     - | Encoding type for the specified picture.
   * - | poc
     - | i32
     - | Picture Order Count (POC): the display order count of the current picture.
   * - | gopConfigs
     - | VCEncGopConfig
     - | This structure defines all Group of Pictures (GOP) structures that will be used
       | throughout the sequence.
   * - | gopSize
     - | i32
     - | Number of pictures within the current GOP.
   * - | gopPicIdx
     - | i32
     - | Encoding order of current picture within its GOP, ranging from 0 to gopSize-1
   * - | roiMapDeltaQpAddr
     - | u32
     - | Pointer (physical address) of QpDelta map, which will be used to store Region of
       | Interest (ROI) map delta Quantization Parameter (QP). Must be a linear, memory
       | residing buffer and 16-byte aligned.
       | The minimum buffer size is:
       | number of CTBs * 32 bytes.
   * - | lineBufWrCnt
     - | u32
     - | For low latency: the number of MB lines already in input MB line buffer.

.. _VCEncOut:

VCEncOut
--------

This data structure is common for all the API calls which generate a
stream. Not all fields are used when starting a stream.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncOut Members
     - | Type
     - | Description
   * - | codingType
     - | VCEncPictureCodingType
     - | Encoding type for the specified picture.
   * - | streamSize
     - | u32
     - | Actual size of the generated stream in bytes is stored here. If the call was
       | unsuccessful then this value is not relevant and it shall be ignored.
   * - | pNaluSizeBuf
     - | u32
     - | Pointer to a buffer where the encoder has stored the sizes of all the created
       | Network Abstraction Layer (NAL) units. A zero value is stored after the last NAL
       | unit size value.
   * - | numNalus
     - | u32
     - | Number of NAL units created.
   * - | busScaledLuma
     - | u32
     - | Bus address of the buffer where the down-scaled picture is stored.
       | The picture format is YCbYCr422_INTERLEAVED.
   * - | scaledPicture
     - | u8
     - | Pointer to a buffer where the down-scaled picture is stored.
   * - | cuOutData
     - | VCEncHEVCCuOutData
     - | Coding Unit (CU) information of a picture output by hardware.

.. _VCEncPictureArea:

VCEncPictureArea
----------------

A structure defining the rectangular area of a Coding Tree Block (CTB).

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncPictureArea Members
     - | Type
     - | Description
   * - | enable
     - | u32
     - | Enable the rectangular area of a CTB.
       | Valid value range: [0,1]
       | 0: Disable
       | 1: Enable
   * - | top
     - | u32
     - | Y-coordinate of the upper-left corner of the rectangular area of the CTB.
       | Valid value range: [0, ctbPerColumn-1].
   * - | left
     - | u32
     - | X-coordinate of the upper-left corner of the rectangular area of the CTB.
       | Valid value range: [0, ctbPerRow-1].
   * - | bottom
     - | u32
     - | Y-coordinate of the lower-right corner of the rectangular area of the CTB.
       | Valid value range: [top, ctbPerColumn-1].
   * - | right
     - | u32
     - | X-coordinate of the lower-right corner of the rectangular area of the CTB.
       | Valid value range: [left, ctbPerRow-1].

.. _VCEncPreProcessingCfg:

VCEncPreProcessingCfg
---------------------

A structure which contains a pre-processing block's parameters.

For more information, refer to Video Pre-Processor Usage Section 4.7. If
an invalid configuration value will be set, the cropping block will keep
its old configuration.

When the video stabilization is in use, the xOffset and yOffset values
hold the latest stabilization result.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncPreProcessingCfg Members
     - | Type
     - | Description
   * - | origWidth
     - | u32
     - | Input image's full width.
       | This size must be equal to or larger than the encoded image's width specified during
       | the encoder initialization phase (See VCEncInit()).
       | A zero value disables the cropping. It is also restricted depending on the input
       | image format (See Video Frame Storage Format). Horizontal stride is assumed to be
       | the next multiple of 16 larger than or equal to this value.
       | Valid value range: 0 or [130, 8192]
   * - | origHeight
     - | u32
     - | Input image's full height.
       | This size must be larger than or equal to the encoded image's height specified at
       | the encoder initialization phase (See VCEncInit()).
       | A zero value disables the cropping.
       | Valid value range: 0 or [130, 8192]
   * - | xOffset
     - | u32
     - | Horizontal offset from the top-left corner of the input image to the top-left
       | corner of the encoded image.
       | Note: the minimum encoded picture width is 130.
       | Valid value range: [0, 8062]
   * - | yOffset
     - | u32
     - | Vertical offset from the top-left corner of the input image to the top-left corner
       | of the encoded image.
       | Note: the minimum encoded picture height is 130.
       | Valid value range: [0, 8062]
   * - | inputType
     - | VCEncPictureType
     - | Input picture format type.
   * - | rotation
     - | VCEncPictureRotation
     - | YUV picture rotation before encoding.
   * - | colorConversion
     - | VCEncColorConversion Enumeration
     - | Structure that specifies the RGB to YUV conversion to be used.
   * - | scaledWidth
     - | u32
     - | Width of scaled picture.
   * - | scaledHeight
     - | u32
     - | Height of scaled picture.
   * - | scaledOutput
     - | u32
     - | Enables or disables the picture down-scaling function.
   * - | scaledOutputFormat
     - | U32
     - | 0:YUV422   1:YUV420SP
   * - | virtualAddressScaledBuff
     - | u32
     - | Virtual address of scaled picture buffer.
   * - | busAddressScaledBuff
     - | u32
     - | Bus address of scaled picture buffer.
   * - | sizeScaledBuff
     - | u32
     - | Size of scaled picture buffer.

.. _VCEncPPSCfg:

VCEncPPSCfg
-----------

A structure that contains the user-provided Picture Parameter Set (PPS) parameters.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncPPSCfg Members
     - | Type
     - | Description
   * - | chroma_qp_offset
     - | i32
     - | Chroma Quantization Parameter (QP) offset.
   * - | tc_Offset
     - | i16
     - | Deblock parameter, tc_offset.
   * - | beta_Offset
     - | i16
     - | Deblock parameter, beta_offset.

.. _VCEncRateCtrl:

VCEncRateCtrl
-------------

A structure containing the rate control parameters.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncRateCtrl Members
     - | Type
     - | Description
   * - | pictureRc
     - | u32
     - | Enables picture level rate control to adjust QP between frames.
       | This should be enabled if target bit rate is set.
       | Valid value range: [0, 1]
       | 0 = disable
       | 1 = enable
       | Default value: 0
   * - | ctbRc
     - | u32
     - | Enables CTB level rate control to adjust QP between CTBs within frame.
       | Valid value range: [0, 1]
       | 0 = disable
       | 1 = enable
       | Default value: 0
   * - | blockRCSize
     - | u32
     - | Set the unit of block size to adjust QP.
       | Valid value range for HEVC: [0, 2]
       | Valid value range for H.264: [2, 2]
       | 0: 64x64
       | 1: 32x32
       | 2: 16x16
   * - | pictureSkip
     - | u32
     - | Allow rate control to skip pictures if not enough bits are available.
       | When Hypothetical Reference Decoder (HRD) is enabled, the rate control may have to
       | skip frames despite this value.
       | Valid value range: [0, 1]
   * - | qpHdr
     - | i32
     - | The initial Quantization Parameter (QP) used by the encoder. If the rate control is
       | enabled then this value is used only at the beginning of the encoding process.
       | When the rate control is disabled then this QP value is used all the time.
       | -1 lets RC calculate initial QP.
       | Not recommended to be set lower than 10.
       | Valid value range: -1 or [0, 51]
       | Default value: 26
   * - | qpMin
     - | u32
     - | The minimum QP that can be set by the RC in the stream.
       | Not recommended to be set lower than 10.
       | Valid value range: [0, 51]
       | Default value: 10
   * - | qpMax
     - | u32
     - | The maximum QP that can be set by the RC in the stream.
       | Valid value range: [qpMin, 51]
       | Default value: 51
   * - | bitPerSecond
     - | u32
     - | The target bit rate in bits per second (bps) when the rate control is enabled.
       | The rate control is considered enabled when pictureRc, pictureSkip or hrd is enabled.
       | When HRD is enabled the bitrate must be within the limits set for the encoder level
       | (refer to :ref:`Levels table <table_encman_hevc-levels>`).
       | Valid value range: [10000, 60000000]
   * - | hrd
     - | u32
     - | Enables the use of Hypothetical Reference Decoder (HRD) model to restrict the
       | instantaneous bitrate. Enabling the HRD will automatically enable the picture
       | rate control.
       | Valid value range: [0,1]
   * - | hrdCpbSize
     - | u32
     - | Size in bits of the Coded Picture Buffer (CPB) used by the HRD model.
       | When HRD is enabled an encoded frame can't be bigger than CPB.
       | By default the encoder will use the maximum allowed size for the initialized
       | encoder level (refer to :ref:`Levels table <table_encman_hevc-levels>`).
       | Setting this value to 0 will always restore the default size.
       | Valid value range: [0, MaxCPB]
   * - | gopLen
     - | u32
     - | Length of the Group Of Pictures (GOP). Rate control calculates bit reserve for
       | this GOP length. Recommended value depends on the video use case
       | (refer to :ref:`reccomended settings <table_encman_recommended_rate_control_settings>`).
       | Valid value range: [1, 300]
       | Default value: 1
   * - | intraQpDelta
     - | i32
     - | Delta value added to the Intra frame QP. Min/Max range checking still applies.
       | Can be used to lower the Intra picture encoded size (higher QP) or to increase
       | Intra quality relative to the Inter pictures (lower QP) to get rid of intra
       | flashing.
       | Valid value range: [-12, 12]
       | Default value: -1
   * - | fixedIntraQp
     - | u32
     - | Use this value for all Intra picture quantization. Value 0 disables the feature.
       | Min/Max range checking still applies. intraQpDelta does not apply when
       | fixedIntraQp is in use.
       | Valid value range: [0, 51]
       | Default value: 0
   * - | bitVarRangeI
     - | i32
     - | Specifies permitting percentage variations over average bits per frame calculated
       | from target bitrates for I frame.
       | 2000 means permitting 2000% variations over average bits per frame for I frame.
       | Valid value range: [10, 2000]
       | Default value: 2000
   * - | bitVarRangeP
     - | i32
     - | Specifies permitting percentage variations over average bits per frame calculated
       | from target bitrates for P frame.
       | 2000 means 2000% over average bits per frame for P frame.
       | Valid value range: [10, 2000]
       | Default value: 2000
   * - | bitVarRangeB
     - | i32
     - | Specifies permitting percentage variations over average bits per frame calculated
       | from target bitrates for B frame. 2000 means permitting 2000% over average bits
       | per frame for B frame.
       | Valid value range: [10, 2000]
       | Default value: 2000
   * - | tolMovingBitRate
     - | i32
     - | Specifies percentage tolerance over target bitrate. 2000 means moving bit rate
       | can tolerate maximal 2000% over target bitrate.
       | Valid value range: [0, 2000]
       | Default value: 2000
   * - | monitorFrames
     - | i32
     - | Specifies how many frames will be monitored for moving bit rate.
       | Valid value range: [10, 120]
       | Default value: frame rate.

.. _VCEncSliceReady:

VCEncSliceReady
---------------

A structure which contains information to pass to the slice ready
callback function. This callback function is called after a slice
encoding has been finished by the hardware.

typedef void (\*VCEncSliceReadyCallBackFunc)(VCEncSliceReady \*);

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncSliceReady Members
     - | Type
     - | Description
   * - | slicesReadyPrev
     - | u32
     - | Number of slices that were ready at the time of previous callback.
   * - | slicesReady
     - | u32
     - | Number of slices that are ready and accessible in the output buffer.
   * - | nalUnitInfoNum
     - | u32
     - | Number of information Network Abstraction Layer (NAL) units are completed,
       | including all kinds of Supplemental Enhancement Information (SEI).
   * - | sliceSizes
     - | u32
     - | Pointer to a buffer where the encoder has stored the sizes (in bytes) of all the
       | finished slices.
   * - | pOutBuf
     - | u32
     - | Pointer to the beginning of the output stream buffer storing the generated stream.
   * - | pAppData
     - | void
     - | Pointer to application specific data.

.. _VCEncGopPicConfig:

VCEncGopPicConfig
-----------------

A structure which contains the GOP structure of one picture.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncGopPicConfig Members
     - | Type
     - | Description
   * - | poc
     - | u32
     - | Picture Order Count (POC): Display order count within a GOP, ranging from 1 to gopSize.
   * - | QpOffset
     - | i32
     - | Quantization Parameter (QP) offset is added to the QP parameter to set the final QP value.
   * - | QpFactor
     - | double
     - | Weight used during rate distortion optimization.
       | Higher values mean lower quality and less bits.
   * - | temporalId
     - | i32
     - | temporal ID of frame in multi-layer stream.
       | (H.264 not supported yet)
   * - | codingType
     - | VCEncPictureCodingType
     - | Picture coding type, can be either Predicted or Bidirectional Predicted.
   * - | numRefPics
     - | u32
     - | Number of reference pictures kept for this picture, including references pictures
       | used by current and future pictures.
   * - | refPics[VCENC_MAX_REF_FRAMES]
     - | VCEncGopPicRps
     - | An array that defines the Reference Picture Sets (RPS) of this picture. Each array
       | member describes one reference picture, so the number of valid array member is numRefPics.

.. _VCEncGopPicRps:

VCEncGopPicRps
--------------

A structure which contains one reference picture.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncGopPicRps Members
     - | Type
     - | Description
   * - | ref_pic
     - | i32
     - | Delta Picture Order Count (POC) of this reference picture, relative to the POC
       | of the current picture.
   * - | used_by_cur
     - | u32
     - | Specifies whether this reference picture is used by the current picture (1) or not (0).

.. _VuiColorDescription:

VuiColorDescription
-------------------

Color description in the vui which coded in the sps(sequence parameter sets).

Only valid when video signal type present flag in the vui is set.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VuiColorDescription Members
     - | Type
     - | Description
   * - | vuiColorDescripPresentFlag
     - | 8u
     - | Color description present in the vui.0- not present, 1- present
   * - | vuiColorPrimaries
     - | 8u
     - | Color's Primaries
   * - | vuiTransferCharacteristics
     - | U8
     - | Transfer Characteristics
   * - | vuiMatrixCoefficients
     - | U8
     - | Matrix Coefficients