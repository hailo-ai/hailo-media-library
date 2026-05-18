Video Encoder Functions
=======================

These functions are not listed in alphabetical order, but rather in an
order in which they are likely to be used.

VCEncGetApiVersion
------------------

Description:

Returns the encoder's API version information.

Syntax:

VCEncApiVersion **VCEncGetApiVersion** (void);

Parameters:

+-----------------------+-----------------------------------------------+
| None                  |                                               |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncApiVersion: <VCEncApiVersion>` a structure containing the API's
major and minor version number.

VCEncGetBuild
-------------

Description:

Returns the hardware and software build information of the encoder.

Does not require encoder initialization.

Syntax:

VCEncBuild **VCEncGetBuild** ( void );

Parameters:

+-----------------------+-----------------------------------------------+
| None                  |                                               |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncBuild <VCEncBuild>` structure

.. _vcencinit:

VCEncInit
---------

Description:

Initializes the encoder and returns an instance of it. The configuration
contains stream parameters that can't be altered during encoding.

Syntax:

VCEncRet **VCEncInit** (

   const VCEncConfig \*pEncCfg,

   VCEncInst \*instAddr

);

Parameters:

+-----------------------+-----------------------------------------------+
| **pEncCfg**           | Points to the :ref:`VCEncConfig <VCEncConfig>`|
|                       | structure that contains the encoder's initial |
| **instAddr**          | configuration parameters.                     |
|                       |                                               |
|                       | Points to a space where the new encoder       |
|                       | instance pointer will be stored.              |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INVALID_ARGUMENT, VCENC_MEMORY_ERROR, VCENC_EWL_ERROR,
VCENC_EWL_MEMORY_ERROR

.. _vcencrelease:

VCEncRelease
------------

Description:

Releases an encoder instance. This will free all the resources allocated
at the encoder initialization phase.

Syntax:

VCEncRet **VCEncRelease** (

   VCEncInst inst

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The encoder instance to be released. This     |
|                       | instance was created earlier with a call to   |
|                       | :ref:`VCEncInit <vcencinit>`\ ().             |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR

.. _vcencsetcodingctrl:

VCEncSetCodingCtrl
------------------

Description:

Sets the encoder's coding parameters. All of the parameters can be
adjusted before the stream is started. The following parameters can also
be altered between frames:

-  sliceSize

-  cirStart

-  cirInterval

-  intraArea

-  roiDeltaQp

If the following parameters need to be altered between frames,

-  roiArea

-  roiMapDeltaQpEnable

-  gdrDuration

cu_qp_delta_enabled_flag in PPS will be reconfigured, so the following
steps should be done:

-  :ref:`VCEncStrmEnd <VCEncStrmEnd>`\ () : to end the last sequence

-  :ref:`VCEncSetCodingCtrl() <vcencsetcodingctrl>` : to change parameters

-  :ref:`VCEncStrmStart <VCEncStrmStart>`\ () to output a new Picture
      Parameter Set (PPS).

then, call

-  :ref:`VCEncStrmEncode <VCEncStrmEncode>`\ () to encode the next frame.

Syntax:

VCEncRet **VCEncSetCodingCtrl** (

   VCEncInst inst,

   const VCEncCodingCtrl \*pCodeParams

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pCodeParams**       | Pointer to the                                |
|                       | :ref:`VCEncCodingCtrl <VCEncCodingCtrl>`      |
|                       | structure that contains the encoder's coding  |
|                       | parameters.                                   |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT

.. _vcencgetcodingctrl:

VCEncGetCodingCtrl
------------------

Description:

Returns the current coding parameters in use by the encoder.

Syntax:

VCEncRet **VCEncGetCodingCtrl** (

   VCEncInst inst,

   const VCEncCodingCtrl \*pCodeParams

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pCodeParams**       | Pointer to the VCEncCodingCtrl structure      |
|                       | where the encoder's coding parameters will be |
|                       | saved.                                        |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR

.. _vcencsetratectrl:

VCEncSetRateCtrl
----------------

Description:

Sets the rate control parameters of the encoder. The function should be
called before starting the stream.

If the Hypothetical Reference Decoder (HRD) is disabled, the function
can also be called between frames to set new rate control parameters
except the ctbRc parameter. This however will reset the rate control and
is recommended to do so at the end of Group of Pictures (GOP). To modify
ctbRc between frames, cu_qp_delta_enabled_flag in the Picture Parameter
Set (PPS) will be reconfigured, so the following steps should be done:

-  :ref:`VCEncStrmEnd <vcencstrmend>`\ (): to end the last sequence

-  :ref:`VCEncSetRateCtrl: <vcencsetratectrl>`\ () to change ctbRc

-  :ref:`VCEncStrmStart <VCEncStrmStart>`\ () to output a new PPS.

Then, call

-  :ref:`VCEncStrmEncode <VCEncStrmEncode>`\ (): to encode the next frame.

In general, ctbRc should not be changed between frames.

Syntax:

VCEncRet **VCEncSetRateCtrl** (

   VCEncInst inst,

   const VCEncRateCtrl \*pRateCtrl

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pRateCtrl**         | Pointer to the                                |
|                       | :ref:`VCEncRateCtrl <VCEncRateCtrl>` structure|
|                       | where the new rate control parameters are     |
|                       | set.                                          |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT, VCENC_INVALID_STATUS

.. _vcencgetratectrl:

VCEncGetRateCtrl
----------------

Description:

Returns the current rate control parameters in use by the encoder.

Syntax:

VCEncRet **VCEncGetRateCtrl** (

   VCEncInst inst,

   const VCEncRateCtrl \*pRateCtrl

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pRateCtrl**         | Pointer to the                                |
|                       | :ref:`VCEncRateCtrl <VCEncRateCtrl>` structure|
|                       | where the new rate control parameters will be |
|                       | saved.                                        |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR

.. _vcencstrmstart:

VCEncStrmStart
--------------

Description:

Starts a new stream and generates the Video Parameter Set (VPS),
Sequence Parameter Set (SPS) and Picture Parameter Set (PPS). The VPS is
the first Network Abstraction Layer (NAL) unit, SPS is the second NAL
unit and PPS is the third NAL unit.

Syntax:

VCEncRet **VCEncStrmStart** (

   VCEncInst inst,

   const VCEncIn \*pEncIn,

   VCEncOut \*pEncOut

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pEncIn**            | Pointer to the :ref:`VCEncIn <VCEncIn>`       |
|                       | structure where the input parameters are      |
|                       | provided.                                     |
+-----------------------+-----------------------------------------------+
| **pEncOut**           | Pointer to the :ref:`VCEncOut <VCEncOut>`     |
|                       | structure where the output parameters will be |
|                       | stored.                                       |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT, VCENC_INVALID_STATUS,
VCENC_OUTPUT_BUFFER_OVERFLOW

.. _vcencstrmencode:

VCEncStrmEncode
---------------

Description:

Encodes a video frame.

Syntax:

VCEncRet **VCEncStrmEncode** (

   VCEncInst inst,

   const VCEncIn \*pEncIn,

   VCEncOut \*pEncOut,

   VCEncSliceReadyCallBackFunc cbFunc,

void \*pAppData )

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pEncIn**            | Pointer to the :ref:`VCEncIn <VCEncIn>`       |
|                       | structure where the input parameters are      |
|                       | provided.                                     |
+-----------------------+-----------------------------------------------+
| **pEncOut**           | Pointer to the :ref:`VCEncOut <VCEncOut>`     |
|                       | structure where the output parameters will be |
|                       | saved.                                        |
+-----------------------+-----------------------------------------------+
| **cbFunc**            | Pointer to the VCEncSliceReady CallBackFunc   |
|                       | callback function that will be called after a |
|                       | slice encoding has been finished by the       |
|                       | hardware.                                     |
+-----------------------+-----------------------------------------------+
| **pAppdata**          | Pointer to application-specific data to be    |
|                       | passed on to the callback function.           |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_FRAME_READY,
VCENC_NULL_ARGUMENT, VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT,
VCENC_INVALID_STATUS, VCENC_OUTPUT_BUFFER_OVERFLOW, VCENC_HW_TIMEOUT,
VCENC_HW_BUS_ERROR, VCENC_HW_RESET, VCENC_HW_RESERVED,
VCENC_SYSTEM_ERROR

.. _vcencstrmend:

VCEncStrmEnd
------------

Description:

Ends a previously started stream. Stream end consists of a Network
Abstraction Layer (NAL) unit of type 'End-of-Sequence'. After a stream
is ended, a new encoding sequence must begin with
:ref:`VCEncStrmStart <VCEncStrmStart>`\ ().

Syntax:

VCEncRet **VCEncStrmEnd** (

   VCEncInst inst,

   const VCEncIn \*pEncIn,

   VCEncOut \*pEncOut

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pEncIn**            | Pointer to the :ref:`VCEncIn <VCEncIn>`       |
|                       | structure where the input parameters are      |
|                       | provided.                                     |
+-----------------------+-----------------------------------------------+
| **pEncOut**           | Pointer to the :ref:`VCEncOut <VCEncOut>`     |
|                       | structure where the output parameters will be |
|                       | saved.                                        |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT, VCENC_INVALID_STATUS

.. _vcencsetpreprocessing:

VCEncSetPreProcessing
---------------------

Description:

Sets the pre-processing block's parameters.

Syntax:

VCEncRet **VCEncSetPreProcessing** (

   VCEncInst inst,

   const VCEncPreProcessingCfg \*pProcCfg

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pPreProcCfg**       | Pointer to the                                |
|                       | :ref:`VCEnc                                   |
|                       | PreProcessingCfg <VCEncPreProcessingCfg>`     |
|                       | structure where the new parameters are set.   |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT, VCENC_SYSTEM_ERROR

.. _vcencgetpreprocessing:

VCEncGetPreProcessing
---------------------

Description:

Returns the current pre-processing block's parameters in use by the
encoder.

Syntax:

VCEncRet **VCEncGetPreProcessing** (

   VCEncInst inst,

   VCEncPreProcessingCfg \*pProcCfg

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pPreProcCfg**       | Pointer to the VCEncPreProcessingCfg          |
|                       | structure where the new parameters are saved. |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR

VCEncSetSeiUserData
-------------------

Description:

Enables or disables writing user data to the encoded stream. The user
data will be written in the stream as a Supplemental Enhancement
Information (SEI) message connected to all the following encoded frames.
The SEI message payload type is marked as user_data_unregistered.

Syntax:

VCEncRet **VCEncSetSeiUserData** (

   VCEncInst inst,

   const u8 \*pUserData,

   u32 userDataSize

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The instance that defines the encoder in use. |
+-----------------------+-----------------------------------------------+
| **pUserData**         | Pointer to a buffer containing the user data. |
|                       | The encoder stores a pointer to this buffer   |
|                       | and reads data from the buffer during         |
|                       | encoding of the following frames. Because the |
|                       | encoder reads data straight from this buffer, |
|                       | it must not be freed before disabling the     |
|                       | user data writing.                            |
+-----------------------+-----------------------------------------------+
| **userDataSize**      | Size of the data in the pUserData buffer in   |
|                       | bytes. If a zero value is given, the user     |
|                       | data writing is disabled. Invalid value       |
|                       | disables user data writing.                   |
|                       |                                               |
|                       | Valid value range: 0 or [16, 2048]            |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR

VCEncGetPerformance
-------------------

Description:

Returns the current picture hardware performance.

Syntax:

u32 **VCEncGetPerformance** (

   VCEncInst inst

);

Parameters:

+-----------------------+-----------------------------------------------+
| inst                  | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`.               |
+-----------------------+-----------------------------------------------+

Returns

The number of clock cycles used to encode the previous frame.

VCEncHEVCGetCuInfo [HEVC only]
------------------------------

Description:

Returns the encoding information of a Coding Unit(CU) in a Coding Tree
Unit (CTU). This function is only valid for HEVC.

Syntax:

VCEncRet **VCEncHEVCGetCuInfo** (

   VCEncInst inst,

VCEncHEVCCuOutData \*pEncCuOutData,

VCEncHEVCCuInfo \*pEncCuInfo,

u32 ctuNum,

u32 cuNum)

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`\ *.*           |
+-----------------------+-----------------------------------------------+
| **pEncCuOutData**     | Pointer to the                                |
|                       | :ref:`VCEncHEVCCuOutData <VCEncHEVCCuOutData>`|
|                       | structure containing the CTU table and CU     |
|                       | information stream output by the hardware.    |
+-----------------------+-----------------------------------------------+
| **pEncCuInfo**        | Pointer to the                                |
|                       | :ref:`VCEncHEVCCuInfo <VCEncHEVCCuInfo>`      |
|                       | structure returning the parsed CU             |
|                       | information.                                  |
+-----------------------+-----------------------------------------------+
| **ctuNum**            | CTU number within picture.                    |
+-----------------------+-----------------------------------------------+
| **cuNum**             | CU number within CTU.                         |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_INVALID_ARGUMENT

VCEncSetInputMBLines
--------------------

Description:

Low latency: sets the valid input MB lines for the encoder to work. This
function is only valid for HEVC.

Syntax:

VCEncRet **VCEncSetInputMBLines** (

   VCEncInst inst,

u32 lines)

);

Parameters:

+-----------------------+-----------------------------------------------+
| inst                  | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`.               |
+-----------------------+-----------------------------------------------+
| lines                 | Valid input MB lines to set.                  |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, NULL_ARGUMENT,
VCENC_INVALID_ARGUMENT

VCEncGetEncodedMbLines
----------------------

Description:

Low latency: returns the encoded lines information from the encoder.
This function is only valid for HEVC.

Syntax:

U32 **VCEncGetEncodedMbLines** (

   VCEncInst inst

);

Parameters:

+-----------------------+-----------------------------------------------+
| i\ **nst**            | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`\ *.*           |
+-----------------------+-----------------------------------------------+
|                       |                                               |
+-----------------------+-----------------------------------------------+

Returns

The encoded lines information from the encoder.

VCEncCreateNewPPS
-----------------

Description:

Creates a new Picture Parameter Set (PPS).

Syntax:

VCEncRet **VCEncCreateNewPPS** (

   VCEncInst inst,

   const VCEncPPSCfg \*pPPSCfg,

   i32 \*newPPSId

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`.               |
+-----------------------+-----------------------------------------------+
| **pPPSCfg**           | Pointer to :ref:`VCEncPPSCfg <VCEncPPSCfg>`   |
|                       | structure with user-provided PPS parameters.  |
+-----------------------+-----------------------------------------------+
| **newPPSId**          | New PPS ID for user.                          |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT

VCEncModifyOldPPS
-----------------

Description:

Modifies an existing Picture Parameter Set (PPS).

Syntax:

VCEncRet **VCEncModifyOldPPS** (

   VCEncInst inst,

   const VCEncPPSCfg \*pPPSCfg,

   i32 ppsId

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`.               |
+-----------------------+-----------------------------------------------+
| **pPPSCfg**           | Pointer to :ref:`VCEncPPSCfg <VCEncPPSCfg>`   |
|                       | structure with user-provided PPS parameters.  |
+-----------------------+-----------------------------------------------+
| **ppsId**             | Old PPS ID provided by the user.              |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT

VCEncGetPPSData
---------------

Description:

Returns the Picture Parameter Set (PPS) data for the user.

Syntax:

VCEncRet **VCEncGetPPSData** (

   VCEncInst inst,

   const VCEncPPSCfg \*pPPSCfg,

   i32 ppsId

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`\ *.*           |
+-----------------------+-----------------------------------------------+
| **pPPSCfg**           | Pointer to :ref:`VCEncPPSCfg <VCEncPPSCfg>`   |
|                       | structure with user-provided PPS parameters   |
|                       | returned.                                     |
+-----------------------+-----------------------------------------------+
| **ppsId**             | PPS ID provided by user.                      |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT

VCEncActiveAnotherPPS
---------------------

Description:

Activates another Picture Parameter Set (PPS) for subsequent frames.

Syntax:

VCEncRet **VCEncActiveAnotherPPS** (

   VCEncInst inst,

   i32 ppsId

);

Parameters:

+-----------------------+-----------------------------------------------+
| **inst**              | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`.               |
+-----------------------+-----------------------------------------------+
| **ppsId**             | PPS ID provided by user.                      |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT

VCEncGetActivePPSId
-------------------

Description:

Returns the ID of the active Picture Parameter Set (PPS).

Syntax:

VCEncRet **VCEncGetActivePPSId** (

   VCEncInst inst,

   i32 \*ppsId

);

Parameters:

+-----------------------+-----------------------------------------------+
| inst                  | The encoder instance for which the            |
|                       | performance is reported. The instance was     |
|                       | created earlier with a call to                |
|                       | :ref:`VCEncInit() <vcencinit>`.               |
+-----------------------+-----------------------------------------------+
| ppsId                 | PPS ID returned to user.                      |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT

1. Additional Test-Related Functions

VCEncInitGopConfigs
-------------------

Description:

Initializes the Group of Pictures (GOP) configuration.

Syntax:

static int **VCEncInitGopConfigs** (

   int gopSize,

   commandLine_s \*cml,

   VCEncGopConfig \*gopCfg,

   u8 \*gopCfgOffset

);

Parameters:

+-----------------------+-----------------------------------------------+
| **gopSize**           | GOP size.                                     |
+-----------------------+-----------------------------------------------+
| **cml**               |                                               |
+-----------------------+-----------------------------------------------+
| **gopCfg**            | Pointer to the                                |
|                       | :ref:`VCEncGopConfig <VCEncGopConfig>`        |
|                       | structure.                                    |
+-----------------------+-----------------------------------------------+
| **gopCfgOffset**      |                                               |
+-----------------------+-----------------------------------------------+

Returns

:ref:`VCEncRet <VCEncRet>` value: VCENC_OK, VCENC_NULL_ARGUMENT,
VCENC_INSTANCE_ERROR, VCENC_INVALID_ARGUMENT
