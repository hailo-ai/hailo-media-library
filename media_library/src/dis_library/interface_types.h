/**
* Copyright 2020 (C) Hailo Technologies Ltd.
* All rights reserved.
*
* Hailo Technologies Ltd. ("Hailo") disclaims any warranties, including, but not limited to,
* the implied warranties of merchantability and fitness for a particular purpose.
* This software is provided on an "AS IS" basis, and Hailo has no obligation to provide maintenance,
* support, updates, enhancements, or modifications.
*
* You may use this software in the development of any project.
* You shall not reproduce, modify or distribute this software without prior written permission.
**/
/**
* @file interface_types.h
* @brief Contains interface types for digital image stabilization and comments about their usage.
**/
#ifndef _DIS_INTERFACE_TYPES_H_
#define _DIS_INTERFACE_TYPES_H_

#include <stddef.h>
#include <stdint.h>

/// Flip vertical, mirror horizontal, rotate to 90/180/270 deg.
/// Flip and mirror may be before or after rotation. All possible combinations
/// end up into 8 possible rotation matrices, listed below
enum FlipMirrorRot {
    NATURAL             = 0, // {1, 0; 0, 1}
    FLIPV_MIRROR_ROT180 = 0, // {1, 0; 0, 1}
    ROT180_FLIPV_MIRROR = 0, // {1, 0; 0, 1}
    ROT90               = 1, // {0, -1; 1, 0}
    FLIPV_MIRROR_ROT270 = 1, // {0, -1; 1, 0}
    ROT270_FLIPV_MIRROR = 1, // {0, -1; 1, 0}
    ROT180              = 2, // {-1, 0; 0, -1}
    FLIPV_MIRROR        = 2, // {-1, 0; 0, -1}
    MIRROR_FLIPV        = 2, // {-1, 0; 0, -1}
    ROT270              = 3, // {0, 1; -1, 0}
    FLIPV_MIRROR_ROT90  = 3, // {0, 1; -1, 0}
    ROT90_FLIPV_MIRROR  = 3, // {0, 1; -1, 0}
    MIRROR              = 4, // {-1, 0; 0, 1}
    FLIPV_ROT180        = 4, // {-1, 0; 0, 1}
    ROT180_FLIPV        = 4, // {-1, 0; 0, 1}
    MIRROR_ROT270       = 5, // {0, -1; -1, 0}
    FLIPV_ROT90         = 5, // {0, -1; -1, 0}
    ROT90_MIRROR        = 5, // {0, -1; -1, 0}
    ROT270_FLIPV        = 5, // {0, -1; -1, 0}
    FLIPV               = 6, // {1, 0; 0, -1}
    MIRROR_ROT180       = 6, // {1, 0; 0, -1}
    ROT180_MIRROR       = 6, // {1, 0; 0, -1}
    MIRROR_ROT90        = 7, // {0, 1; 1, 0}
    FLIPV_ROT270        = 7, // {0, 1; 1, 0}
    ROT270_MIRROR       = 7, // {0, 1; 1, 0}
    ROT90_FLIPV         = 7  // {0, 1; 1, 0}
};

/// Return codes of interface functions.
/// Error code are returned whenever an error occurs during the use of the corresponding functionality of the DIS class
/// (more info about the error is printed in the log), otherwise DIS_OK signals that everything is running as expected.
enum RetCodes {
    DIS_OK,             // 0 no error
    ERROR_CTX,          // 1 ctx is NULL or *ctx already points to something in dis_init()
    ERROR_CONFIG,       // 2 error in config file; more info is printed in the log
    ERROR_CALIB,        // 3 error in calibration file; more info is printed in the log
    ERROR_INIT,         // 4 error in dis_init()
    ERROR_GRID,         // 5 error during grid calculation
    ERROR_INPUT_DATA,   // 6 error regarding input data. more info is printed in the log
    ERROR_INTERNAL      // 7 internal error. more info is printed in the log
};

#endif // _DIS_INTERFACE_TYPES_H_
