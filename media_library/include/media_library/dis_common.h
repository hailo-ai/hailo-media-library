/*
* Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
* 
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
* 
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/**
* @file dis_common.h
* @brief Common functions and config structure declaration
*
**/
#ifndef _DIS_COMMON_H_
#define _DIS_COMMON_H_

#define _USE_MATH_DEFINES
#include <algorithm>
#include <math.h>
#include <climits>

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

/*
Projection camera type - 0: pinhole, 1: fisheye., 2: same as input distortions. When cropped from center,
output image will be a croppped and scaled version of the inpout one
If the required output FOV is > 130deg, avoid using pin-hole type - looks bad.
*/
enum camera_type_t
{
  CAMERA_TYPE_PINHOLE = 0,
  CAMERA_TYPE_FISHEYE = 1,
  CAMERA_TYPE_INPUT_DISTORTIONS = 2,

  /** Max enum value to maintain ABI Integrity */
  CAMERA_TYPE_MAX = INT_MAX
};

struct dis_debug_config_t
{
    /// Generates grid which only resizes the input image into the output.
    bool generate_resize_grid;

    ///Fix the stabilized orientation to the values in FIX_STAB_LO, FIX_STAB_LA (usually set to 0). This removes the
    ///impact of stabilization filter and black-corners limitations. If after setting this to 1, the output video is
    ///still unstable, then the reason is most likely not in the DIS settings, but in wrong FMVs.
    bool fix_stabilization;

    ///fixed stabilized longitude and latitude with respect to the first frame, radians
    float fix_stabilization_longitude;
    float fix_stabilization_latitude;
};

struct dis_config_t
{
    // Enable Digital Image Stabilizaton
    bool enabled;

    /*
    Minimal value of the coefficient 'k' used to filter the motion vectors (MVs).
    k takes values in the range [0, 1] and determines how fast we see changes in output from a given MV,
    i.e. the result of the current frame's MV will be seen after 1/k frames.
    Example: k = 0 results in complete filtering and lack of consideration of the current MV,
    k = 1 means immediate impact (on the following frame).
    */
    float minimun_coefficient_filter;

    /*
    Value by which to decrement k whenever the difference of succeeding motion vectors is not too large.
    Roughly, 3* 1/this is the time (number of frames) that will take to adapt the filter to a new, weaker
    shaking amplitude and retrieve the filter strength. Values : 0-1, recommended 1/100-1/10, dimensionless.
    */
    float decrement_coefficient_threshold;

    /*
    Value by which to increment k when large motion occurs to prevent black corners.
    Roughly, 3* 1/this is the time (number of frames) that will take to adapt the filter to a new,
    higher shaking amplitude and minimize limitations for avoiding black corners.
    Values : 0-1, recommended 1/100-1/10, dimensionless.
    */
    float increment_coefficient_threshold;

    /*
    The frame motion vector (MV) is calculated by a HW on each frame and fed into the DIS library. Sometimes this MV
    is very wrong (gross error) - e.g. when the frame brightness change rapidly, or when the scene change rapidly
    (e.g. put a finger on the lens, or frame MV is suddenly very high - e.g. hit the camera.
    Such a gross error affect the stabilization at and after the moment of this error. So, detect gross errors and
    replace the erroneous MV with the MV of the previous frame. The detection works like this:
    On each frame, calculate the runtime average of MV and its standard deviation (STD). "1/running_average_coefficient" is
    roughly the number of frame being averaged. If abs(current_MV - mean_MV) > STD_MULTIPLIER * STD, then this
    sample is a gross error-discard it.
    (0..1], typically "1 / number-of-frames-to-average". 1 to disable.
    */
    float running_average_coefficient;

    // Acceptable deviation, >0, normally 2.5-3.5. Set to very big value to disable
    float std_multiplier;

    ///If the shake is too strong, some frames may be impossible to stabilize without black corners appearing.
    ///Normally, Stabilized position (and output video) jumps in such cases, violating the stabilization, but
    ///avoiding black corners. If desired, The black corners could be left in order to keep smooth output - set to 1.
    ///true: enable, false: disable (smooth stab with black corners)
    bool black_corners_correction_enabled;

    /*
    Filter strength is decreased if the stabilizing rotation is bigger than
    "BLKCRN_TO_K_THR * room-for-stabilization". The lower this coefficient is, the less chance for limitations,
    but the more often the stabilization will be weakened without a real need for this. Also, if a panning starts,
    and k adaptation is disabled (STAB_K_INC_BLKCRN = 0 or BLKCRN_TO_K_THR is much more that 1), the filter
    will follow the panning with too big delay and limitations will appear on each frame. Hence, the stabilized
    video will follow the input one, repeating its shakes along the panning, shifted (looks like delayed) by
    the room for stabilization.
    If BLKCRN_TO_K_THR is between 0 and 1 this panning delay is (1 - BLKCRN_TO_K_THR) * room_for_stabilization.
    Values : 0-1, recommended 0.2-0.5, default 0.2, dimensionless.
    */
    float black_corners_threshold;

    // Debug
    dis_debug_config_t debug;
};

#endif // _DIS_COMMON_H_