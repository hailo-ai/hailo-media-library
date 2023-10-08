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
* @file common.h
* @brief Common functions and config structure declaration
*
**/
#ifndef _DIS_COMMON_H_
#define _DIS_COMMON_H_

#define _USE_MATH_DEFINES
#include <algorithm>
#include <math.h>

/// @brief radians to degrees
///
/// @param rad angle in radians
static inline float DEGREES(float rad){
    return rad * (180.f / float(M_PI));
}
/// @brief degrees to radians
///
/// @param rad angle in degrees
static inline float RADIANS(float deg){
    return deg * ( float(M_PI) / 180.f );
}

/// @brief clamping function
///
/// @param val value to be clamped
/// @param min minimal possible value
/// @param max maximal possible value
template <typename val_t>
static inline val_t clamp(val_t val, val_t min, val_t max) {
    return std::min(std::max(val, min), max);
}


struct Cfg {

//not used so far - FMV history
//    /// Length of circular buffer holding the motion vector history. Must be a power of 2.
//    int FMV_HISTORY_LEN   = 128;
//    /// Derived from FMV_HISTORY_LEN: (1 << FMV_HISTORY_BITS) = MESH_CELL_SIZE_PIX
//    int fmv_history_bits = 5; //this is NOT in read from config file

    /// ------------ output camera --------------

    /// Projection camera type - 0: pinhole, 1: fisheye., 2: same as input distortions. When cropped from center,
    ///output image will be a croppped and scaled version of the inpout one
    ///If the required output FOV is > 130deg, avoid using pin-hole type - looks bad.
    int OUT_CAMERA_TYPE    = 0;

    /// Diagonal FoV of output camera in degrees. The difference between input and output FOV, (horizontal, verticsal
    ///and diagonal) is the room for stabilization. Note the relation betwen aspect ratio and H,V,DFOV ratios:
    /// - for fisheye camera:
    ///  HFOV / VFOV / DFOV = width / hight / diagonal
    /// - for pinhole camera:
    ///  tan(HFOV/2) / tan(VFOV/2) / tan(DFOV/2) = width / hight / diagonal
    /// Set to <=0  to let DIS calculate and use the maximum possible FOV at the given input camera model and output
    ///aspect ratio.
    ///values: pinhole: 1-179, fisheye: or 1-360, degrees, no default, <=0 means "maximum possible FOV"
    float OUT_CAMERA_FOV   = 90;

    /// ------------ stabilization --------------

    /// Minimal value of the coefficient 'k' used to filter the motion vectors (MVs).
    /// k takes values in the range [0, 1] and determines how fast we see changes in output from a given MV,
    /// i.e. the result of the current frame's MV will be seen after 1/k frames.
    /// Example: k = 0 results in complete filtering and lack of consideration of the current MV,
    /// k = 1 means immediate impact (on the following frame).
    float STAB_K_MIN = 0.1;

    /// Value by which to increment k when large motion occurs to prevent black corners.
    /// Roughly, 3* 1/this is the time (number of frames) that will take to adapt the filter to a new,
    ///higher shaking amplitude and minimize limitations for avoiding black corners.
    ///Values : 0-1, recommended 1/100-1/10, dimensionless.
    float STAB_K_INC_BLKCRN = 0.01;

    /// Value by which to decrement k whenever the difference of succeeding motion vectors is not too large.
    ///Roughly, 3* 1/this is the time (number of frames) that will take to adapt the filter to a new, weaker
    ///shaking amplitude and retrieve the filter strength. Values : 0-1, recommended 1/100-1/10, dimensionless.
    float STAB_K_DECR = 0.001;


    ///Filter strength is decreased if the stabilizing rotation is bigger than
    ///"BLKCRN_TO_K_THR * room-for-stabilization". The lower this coefficient is, the less chance for limitations,
    ///but the more often the stabilization will be weakened without a real need for this. Also, if a panning starts,
    ///and k adaptation is disabled (STAB_K_INC_BLKCRN = 0 or BLKCRN_TO_K_THR is much more that 1), the filter
    ///will follow the panning with too big delay and limitations will appear on each frame. Hence, the stabilized
    ///video will follow the input one, repeating its shakes along the panning, shifted (looks like delayed) by
    ///the room for stabilization.
    ///If BLKCRN_TO_K_THR is between 0 and 1 this panning delay is (1 - BLKCRN_TO_K_THR) * room_for_stabilization.
    ///Values : 0-1, recommended 0.2-0.5, default 0.2, dimensionless.
    float BLKCRN_TO_K_THR = 0.2;

    ///If the shake is too strong, some frames may be impossible to stabilize without black corners appearing.
    ///Normally, Stabilized position (and output video) jumps in such cases, violating the stabilization, but
    ///avoiding black corners. If desired, The black corners could be left in order to keep smooth output - set to 1.
    ///1: enable, 0: disable (smooth stab with black corners)
    int BLKCRN_CORRECT_ENB = 1; 

    ///The frame motion vector (MV) is calculated by a HW on each frame and fed into the DIS library. Sometimes this MV
    ///is very wrong (gross error) - e.g. when the frame brightness change rapidly, or when the scene change rapidly
    ///(e.g. put a finger on the lens, or frame MV is suddenly very high - e.g. hit the camera.
    ///Such a gross error affect the stabilization at and after the moment of this error. So, detect gross errors and
    ///replace the erroneous MV with the MV of the previous frame. The detection works like this:
    ///On each frame, calculate the runtime average of MV and its standard deviation (STD). "1/RUNNING_AVG_COEFF" is
    ///roughly the number of frame being averaged. If abs(current_MV - mean_MV) > STD_MULTIPLIER * STD, then this
    ///sample is a gross error-discard it.
    float RUNNING_AVG_COEFF = 0.033;  ///(0..1], typically "1 / number-of-frames-to-average". 1 to disable.
    float STD_MULTIPLIER = 3.f;       ///acceptable deviation, >0, normally 2.5-3.5. Set to very big value to disable

    /// ------------ debug --------------

    /// 1: generates grid which only resizes the input image into the output.
    int GEN_RESIZE_GRID = 0;

    ///Fix the stabilized orientation to the values in FIX_STAB_LO, FIX_STAB_LA (usually set to 0). This removes the
    ///impact of stabilization filter and black-corners limitations. If after setting this to 1, the output video is
    ///still unstable, then the reason is most likely not in the DIS settings, but in wrong FMVs.
    int DEBUG_FIX_STAB = 0;
    ///fixed stabilized longitude and latitude with respect to the first frame, radians
    float FIX_STAB_LO = 0;
    float FIX_STAB_LA = 0;
};


#endif // _DIS_COMMON_H_
