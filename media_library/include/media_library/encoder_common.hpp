/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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
 * @file encoder_common.hpp
 * @brief MediaLibrary Encoder parameter definitions
 **/

#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern "C"
{
#include "video_encoder/base_type.h"
#include "video_encoder/encinputlinebuffer.h"
#include "video_encoder/ewl.h"
#include "video_encoder/hevcencapi.h"
}

/** @defgroup encoder_common_definitions MediaLibrary Encoder Common CPP API
 * definitions
 *  @{
 */

#define MIN_QP_VALUE (0)
#define MAX_QP_VALUE (51)
#define MIN_BITRATE_VARIABLE_RANGE (0)
#define MAX_BITRATE_VARIABLE_RANGE (2000)

#define MIN_BITRATE (10000)
#define MAX_BITRATE (40000000)
#define MIN_HRD_CPB_SIZE (0)
#define MAX_HRD_CPB_SIZE (40000000)
#define MIN_CVBR_MODE (0)
#define MAX_CVBR_MODE (0x7FFFFFFF)
#define AUTO_MONITOR_FRAMES (0)
#define MIN_MONITOR_FRAMES (10)
#define MAX_MONITOR_FRAMES (120)
#define MIN_INTRA_PIC_RATE (0)
#define MAX_INTRA_PIC_RATE (300)
#define MIN_GOP_LENGTH (0)
#define MAX_GOP_LENGTH (300)
#define MIN_GOP_SIZE (1)
// #define MAX_GOP_SIZE (8) - Defined in hevcencapi.h
#define MIN_QPHDR (-1)
#define MAX_QPHDR (MAX_QP_VALUE)
#define MIN_INTRA_QP_DELTA (-MAX_QP_VALUE)
#define MAX_INTRA_QP_DELTA (MAX_QP_VALUE)
#define MIN_FIXED_INTRA_QP (MIN_QP_VALUE)
#define MAX_FIXED_INTRA_QP (MAX_QP_VALUE)
#define MIN_BFRAME_QP_DELTA (-1)
#define MAX_BFRAME_QP_DELTA (MAX_QP_VALUE)

#define DEFAULT_UNCHANGED (-255)
#define DEFAULT_INPUT_FORMAT (VCENC_YUV420_SEMIPLANAR)
#define DEFAULT_HEVC_PROFILE (-1)
#define DEFAULT_HEVC_LEVEL (-1)
#define DEFAULT_H264_PROFILE (-1)
#define DEFAULT_H264_LEVEL (-1)
#define DEFAULT_INTRA_PIC_RATE (60)
#define DEFAULT_GOP_LENGTH (0)
#define DEFAULT_GOP_SIZE (1)
#define DEFAULT_QPHDR (-1)
#define DEFAULT_QPMIN (10)
#define DEFAULT_QPMAX (48)
#define DEFAULT_INTRA_QP_DELTA (-5)
#define DEFAULT_FIXED_INTRA_QP (MIN_QP_VALUE)
#define DEFAULT_BFRAME_QP_DELTA (MIN_BFRAME_QP_DELTA)
#define DEFAULT_BITRATE (25000000)
#define DEFAULT_TOL_MOVING_BITRATE (15)
#define DEFAULT_BITVAR_RANGE_I (2000)
#define DEFAULT_BITVAR_RANGE_P (2000)
#define DEFAULT_BITVAR_RANGE_B (2000)
#define DEFAULT_MONITOR_FRAMES (0)
#define DEFAULT_HRD_CPB_SIZE (0)
#define DEFAULT_CVBR_MODE (0)

/* Smooth bitrate adjustment parameters */
#define DEFAULT_ENABLE_GOP_BITRATE_ADJUSTER (false)
#define DEFAULT_THRESHOLD_HIGH 0.3
#define DEFAULT_THRESHOLD_LOW 0.125
#define DEFAULT_MAX_TARGET_BITRATE_FACTOR 1.3
#define DEFAULT_BITRATE_ADJUSTMENT_FACTOR 0.2

/* QP smooth settings defaults */
#define DEFAULT_QP_SMOOTH_QP_DELTA (128)                 // (0x80)  // default = DEFAULT_QP_DELTA
#define DEFAULT_QP_SMOOTH_QP_DELTA_LIMIT (1536)          //(0x600) // default = DEFAULT_QP_DELTA_LIMIT
#define DEFAULT_QP_SMOOTH_QP_DELTA_INCREMENT (128)       //(0x80)  // default = DEFAULT_QP_DELTA
#define DEFAULT_QP_SMOOTH_QP_DELTA_LIMIT_INCREMENT (384) //(0x180)  // default = DEFAULT_QP_DELTA_LIMIT / 2
#define DEFAULT_QP_SMOOTH_QP_ALPHA (0.0f)                /* Default smoothing ratio */
#define DEFAULT_QP_SMOOTH_Q_STEP_DIVISOR (2)             /* Default Q step divisor */

/* Boost parameters defaults */
#define DEFAULT_BOOST_ENABLED (true)
#define DEFAULT_BOOST_FACTOR (1.4f)
#define DEFAULT_BOOST_TIMEOUT_MS (1000)
#define DEFAULT_BOOST_MAX_BITRATE (16000000)
#define DEFAULT_FORCE_KEYFRAME_ON_ZOOM (true)

/* Constant optical zoom boost parameters defaults */
#define DEFAULT_CONSTANT_OPTICAL_ZOOM_BOOST (true)
#define DEFAULT_CONSTANT_OPTICAL_ZOOM_BOOST_THRESHOLD (2.5f)
#define DEFAULT_CONSTANT_OPTICAL_ZOOM_BOOST_FACTOR (1.2f)
typedef struct
{
    i32 width;
    i32 height;
    VCEncPictureType inputFormat;
    i32 profile;
    i32 level;
    VCEncStreamType streamType;
    i32 frameRateNumer; /* Output frame rate numerator */
    i32 frameRateDenom; /* Output frame rate denominator */
    i32 picture_cnt;
    i32 picture_enc_cnt;
    u32 intra_pic_rate;
    i32 last_idr_picture_cnt;
    u32 validencodedframenumber;
    u32 stride; /* Used for padded buffers, and specify the pad size to crop in each buffer line*/

    i32 max_cu_size;    /* Max coding unit size in pixels */
    i32 min_cu_size;    /* Min coding unit size in pixels */
    i32 max_tr_size;    /* Max transform size in pixels */
    i32 min_tr_size;    /* Min transform size in pixels */
    i32 tr_depth_intra; /* Max transform hierarchy depth */
    i32 tr_depth_inter; /* Max transform hierarchy depth */
    u32 outBufSizeMax;  /* Max buf size in MB */
    u32 roiMapDeltaQpBlockUnit;

    // Rate Control Params
    i32 qphdr;
    u32 qpmin;
    u32 qpmax;
    i32 intra_qp_delta;
    i32 bFrameQpDelta;
    u32 fixed_intra_qp;
    u32 bitrate;
    u32 bitVarRangeI;
    u32 bitVarRangeP;
    u32 bitVarRangeB;
    u32 tolMovingBitRate;
    u32 monitorFrames;
    u32 pictureRc;
    u32 ctbRc;
    u32 blockRcSize; /*size of block rate control : 2=16x16,1= 32x32, 0=64x64*/
    u32 pictureSkip;
    u32 hrd;
    u32 padding;
    u32 cvbr;
    u32 hrdCpbSize;

    u32 compressor;

    /* SW/HW shared memories for output buffers */
    void *ewl;
    EWLLinearMem_t outbufMem;

    float sumsquareoferror;
    float averagesquareoferror;
    i32 maxerrorovertarget;
    i32 maxerrorundertarget;
    long numbersquareoferror;

    char *roiArea1;
    char *roiArea2;

    u32 gopSize;
    u32 gopLength;
    VCEncIn encIn;
    VCEncOut encOut;
    bool codecH264;
    u32 intraPicRate;
    u8 gopCfgOffset[MAX_GOP_SIZE + 1];

    // Slice data
    u8 *strmPtr;
    u32 multislice_encoding;

    // Adaptive Gop variables
    int gop_frm_num;
    double sum_intra_vs_interskip;
    double sum_skip_vs_interskip;
    double sum_intra_vs_interskipP;
    double sum_intra_vs_interskipB;
    int sum_costP;
    int sum_costB;
    int last_gopsize;
    i32 nextGopSize;
    VCEncPictureCodingType nextCodingType;

    /* Smooth bitrate adjustment parameters */
    float gop_anomaly_bitrate_adjuster_high_threshold; /* High threshold for GOP frame analysis */
    float gop_anomaly_bitrate_adjuster_low_threshold;  /* Low threshold for GOP frame analysis */
    float gop_anomaly_bitrate_adjuster_max_factor;     /* Maximum target bitrate factor */
    float gop_anomaly_bitrate_adjuster_factor;         /* Bitrate adjustment factor */
    bool gop_anomaly_bitrate_adjuster_enable;          /* Enable/disable smooth bitrate adjustment, [0,1] */

    /* QP smooth settings parameters */
    i32 qp_smooth_qp_delta;            /* QP smooth QP delta parameter */
    i32 qp_smooth_qp_delta_limit;      /* QP smooth QP delta limit parameter */
    u32 qp_smooth_qp_delta_step;       /* QP smooth QP delta increment parameter */
    u32 qp_smooth_qp_delta_limit_step; /* QP smooth QP delta limit increment parameter */
    float qp_smooth_qp_alpha;          /* QP smooth alpha parameter */
    i32 qp_smooth_q_step_divisor;      /* QP smooth Q step divisor parameter */

    /* Adjust bitrate parameters for optical zoom */
    bool zoom_bitrate_adjuster_enable;         /* Enable/disable boost for optical zoom */
    float zoom_bitrate_adjuster_factor;        /* Bitrate boost factor */
    u32 zoom_bitrate_adjuster_timeout_ms;      /* Boost timeout in milliseconds */
    u32 zoom_bitrate_adjuster_max_bitrate;     /* Maximum bitrate when boosting (0 = no limit) */
    bool zoom_bitrate_adjuster_force_keyframe; /* Force keyframe when optical zoom changes */

    /* Constant optical zoom boost parameters */
    bool constant_optical_zoom_boost;            /* Enable/disable constant boost for optical zoom */
    float constant_optical_zoom_boost_threshold; /* Threshold level for constant boost activation */
    float constant_optical_zoom_boost_factor;    /* Constant boost factor for optical zoom */
} EncoderParams;

/** @} */ // end of encoder_common_definitions
