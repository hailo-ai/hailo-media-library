Numeric Data Types
==================

The following common numeric data types are declared in **base_type.h:**

+---------------------+------------------------------------------------+
| Name                | Data type                                      |
+---------------------+------------------------------------------------+
| u8                  | unsigned 8 bit integer                         |
+---------------------+------------------------------------------------+
| i8                  | signed 8 bit integer                           |
+---------------------+------------------------------------------------+
| u16                 | unsigned 16 bit integer                        |
+---------------------+------------------------------------------------+
| i16                 | signed 16 bit integer                          |
+---------------------+------------------------------------------------+
| u32                 | unsigned 32 bit                                |
+---------------------+------------------------------------------------+
| i32                 | signed 32 bit                                  |
+---------------------+------------------------------------------------+

Return Codes
============

.. _VCEncRet:

VCEncRet Enumeration
--------------------

Specifies the return values for the API functions.

.. list-table::
   :widths: auto
   :header-rows: 1

   * - | VCEncRet Values
     - | Description
   * - VCENC_OK
     - Success.
   * - VCENC_EWL_ERROR
     - Error, the encoder's system interface failed to initialize.
   * - VCENC_EWL_MEMORY_ERROR
     - Error, the system interface failed to allocate memory.
   * - VCENC_FRAME_READY
     - A frame encoding was finished.
   * - VCENC_HW_BUS_ERROR
     - Error. This can be caused by invalid bus addresses that push the encoder to access an invalid memory area. New frame encoding must be started.
   * - VCENC_HW_RESERVED
     - Error, the hardware could not be reserved for exclusive access.
   * - VCENC_HW_RESET
     - Error, the hardware was reset by external means. The whole frame is lost.
   * - VCENC_HW_TIMEOUT
     - Error, the wait for a hardware finish has timed out. The current frame is lost. New frame encoding must be started.
   * - VCENC_INSTANCE_ERROR
     - Error, the encoder instance is invalid or corrupted.
   * - VCENC_INVALID_ARGUMENT
     - Error, one of the arguments was invalid. None of the arguments was set.
   * - VCENC_INVALID_STATUS
     - Error, the stream was started with Hypothetical Reference Decoder (HRD) enabled and consequently, the rate control parameters cannot be altered.
   * - VCENC_MEMORY_ERROR
     - Error, the encoder was not able to allocate memory.
   * - VCENC_NULL_ARGUMENT
     - Error, a pointer argument had an invalid NULL value.
   * - VCENC_OUTPUT_BUFFER_OVERFLOW
     - Error, the output buffer's size was too small to fit the generated stream. Allocate a bigger buffer and try again.
   * - VCENC_SYSTEM_ERROR
     - Error, a fatal system error occurred. The encoding can't continue. The encoder instance must be released.
