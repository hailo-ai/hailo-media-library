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
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <sstream>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <sys/resource.h>
#include <gst/allocators/gstfdmemory.h>
#include "gsthailoenc.hpp"
#include "buffer_utils/buffer_utils.hpp"

/*******************
Property Definitions
*******************/
enum
{
    PROP_0,
    PROP_INTRA_PIC_RATE,
    PROP_GOP_SIZE,
    PROP_GOP_LENGTH,
    PROP_QPHDR,
    PROP_QPMIN,
    PROP_QPMAX,
    PROP_INTRA_QP_DELTA,
    PROP_FIXED_INTRA_QP,
    PROP_BFRAME_QP_DELTA,
    PROP_BITRATE,
    PROP_TOL_MOVING_BITRATE,
    PROP_BITRATE_VAR_RANGE_I,
    PROP_BITRATE_VAR_RANGE_P,
    PROP_BITRATE_VAR_RANGE_B,
    PROP_PICTURE_RC,
    PROP_CTB_RC,
    PROP_PICTURE_SKIP,
    PROP_HRD,
    PROP_CVBR,
    PROP_PADDING,
    PROP_MONITOR_FRAMES,
    PROP_ROI_AREA_1,
    PROP_ROI_AREA_2,
    PROP_COMPRESSOR,
    PROP_BLOCK_RC_SIZE,
    PROP_HRD_CPB_SIZE,
    PROP_ADAPT_FRAMERATE,
    PROP_FRAMERATE_TOLERANCE,
    NUM_OF_PROPS,

    /* Smooth bitrate adjustment parameters */
    PROP_ENABLE_GOP_BITRATE_ADJUSTER,
    PROP_THRESHOLD_HIGH,
    PROP_THRESHOLD_LOW,
    PROP_MAX_TARGET_BITRATE_FACTOR,
    PROP_BITRATE_ADJUSTMENT_FACTOR,

    /* QP smooth settings */
    PROP_QP_SMOOTH_QP_DELTA,
    PROP_QP_SMOOTH_QP_DELTA_LIMIT,
    PROP_QP_SMOOTH_QP_DELTA_INCREMENT,
    PROP_QP_SMOOTH_QP_DELTA_LIMIT_INCREMENT,
    PROP_QP_SMOOTH_QP_ALPHA,
    PROP_QP_SMOOTH_Q_STEP_DIVISOR,

    /* Boost parameters */
    PROP_BOOST_ENABLED,
    PROP_BOOST_FACTOR,
    PROP_BOOST_TIMEOUT_MS,
    PROP_BOOST_MAX_BITRATE,
    PROP_FORCE_KEYFRAME_ON_ZOOM,

    /* Constant optical zoom boost parameters */
    PROP_CONSTANT_OPTICAL_ZOOM_BOOST,
    PROP_CONSTANT_OPTICAL_ZOOM_BOOST_THRESHOLD,
    PROP_CONSTANT_OPTICAL_ZOOM_BOOST_FACTOR,
};

#define MIN_FRAMERATE_TOLERANCE (0)
#define MAX_FRAMERATE_TOLERANCE (500)
#define DEFAULT_FRAMERATE_TOLERANCE (15)

#define GST_TYPE_HAILOENC_COMPRESSOR (gst_hailoenc_compressor_get_type())
static GType gst_hailoenc_compressor_get_type(void)
{
    /*Enable/Disable Embedded Compression
      0 = Disable Compression
      1 = Only Enable Luma Compression
      2 = Only Enable Chroma Compression
      3 = Enable Both Luma and Chroma Compression*/
    static GType hailoenc_compressor_type = 0;
    static const GEnumValue hailoenc_compressors[] = {
        {0, "Disable Compression", "disable"},
        {1, "Only Enable Luma Compression", "enable-luma"},
        {2, "Only Enable Chroma Compression", "enable-chroma"},
        {3, "Enable Both Luma and Chroma Compression", "enable-both"},
        {0, NULL, NULL},
    };
    if (!hailoenc_compressor_type)
    {
        hailoenc_compressor_type = g_enum_register_static("GstHailoEncCompressor", hailoenc_compressors);
    }
    return hailoenc_compressor_type;
}

#define GST_TYPE_HAILOENC_BLOCK_RC_SIZE (gst_hailoenc_block_rc_size_get_type())
static GType gst_hailoenc_block_rc_size_get_type(void)
{
    static GType hailoenc_block_rc_size_type = 0;
    static const GEnumValue hailoenc_block_rc_size_types[] = {
        {0, "64X64", "64x64"},
        {1, "32X32", "32x32"},
        {2, "16X16", "16x16"},
        {0, NULL, NULL},
    };
    if (!hailoenc_block_rc_size_type)
    {
        hailoenc_block_rc_size_type = g_enum_register_static("GstHailoEncBlockRcSize", hailoenc_block_rc_size_types);
    }
    return hailoenc_block_rc_size_type;
}

/*******************
Function Definitions
*******************/

static void gst_hailoenc_class_init(GstHailoEncClass *klass);
static void gst_hailoenc_init(GstHailoEnc *hailoenc);
static void gst_hailoenc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoenc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_hailoenc_finalize(GObject *object);
static void gst_hailoenc_dispose(GObject *object);

static gboolean gst_hailoenc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static gboolean gst_hailoenc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query);
static gboolean gst_hailoenc_flush(GstVideoEncoder *encoder);
static gboolean gst_hailoenc_start(GstVideoEncoder *encoder);
static gboolean gst_hailoenc_stop(GstVideoEncoder *encoder);
static GstFlowReturn gst_hailoenc_finish(GstVideoEncoder *encoder);
static GstFlowReturn gst_hailoenc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);

/*************
Init Functions
*************/

GST_DEBUG_CATEGORY_STATIC(gst_hailoenc_debug);
#define GST_CAT_DEFAULT gst_hailoenc_debug
#define _do_init GST_DEBUG_CATEGORY_INIT(gst_hailoenc_debug, "hailoenc", 0, "hailoenc element");
#define gst_hailoenc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoEnc, gst_hailoenc, GST_TYPE_VIDEO_ENCODER, _do_init);

static void gst_hailoenc_class_init(GstHailoEncClass *klass)
{
    GObjectClass *gobject_class;
    GstVideoEncoderClass *venc_class;

    gobject_class = (GObjectClass *)klass;
    venc_class = (GstVideoEncoderClass *)klass;

    gobject_class->set_property = gst_hailoenc_set_property;
    gobject_class->get_property = gst_hailoenc_get_property;

    g_object_class_install_property(
        gobject_class, PROP_INTRA_PIC_RATE,
        g_param_spec_uint("intra-pic-rate", "IDR Interval", "I frames interval (0 - Dynamic IDR Interval)",
                          MIN_INTRA_PIC_RATE, MAX_INTRA_PIC_RATE, (guint)DEFAULT_INTRA_PIC_RATE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_GOP_SIZE,
        g_param_spec_uint("gop-size", "GOP Size", "GOP Size (1 - No B Frames)", MIN_GOP_SIZE, MAX_GOP_SIZE,
                          (guint)DEFAULT_GOP_SIZE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_GOP_LENGTH,
        g_param_spec_uint("gop-length", "GOP Length", "GOP Length", MIN_GOP_LENGTH, MAX_GOP_LENGTH,
                          (guint)DEFAULT_GOP_LENGTH,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QPHDR,
        g_param_spec_int("qp-hdr", "Initial target QP", "Initial target QP, -1 = Encoder calculates initial QP",
                         MIN_QPHDR, MAX_QPHDR, (gint)DEFAULT_QPHDR,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QPMIN,
        g_param_spec_uint("qp-min", "QP Min", "Minimum frame header QP", MIN_QP_VALUE, MAX_QP_VALUE,
                          (guint)DEFAULT_QPMIN,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QPMAX,
        g_param_spec_uint("qp-max", "QP Max", "Maximum frame header QP", MIN_QP_VALUE, MAX_QP_VALUE,
                          (guint)DEFAULT_QPMAX,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_INTRA_QP_DELTA,
        g_param_spec_int("intra-qp-delta", "Intra QP delta", "QP difference between target QP and intra frame QP",
                         MIN_INTRA_QP_DELTA, MAX_INTRA_QP_DELTA, (gint)DEFAULT_INTRA_QP_DELTA,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_FIXED_INTRA_QP,
        g_param_spec_uint("fixed-intra-qp", "Fixed Intra QP",
                          "Use fixed QP value for every intra frame in stream, 0 = disabled", MIN_FIXED_INTRA_QP,
                          MAX_FIXED_INTRA_QP, (guint)DEFAULT_FIXED_INTRA_QP,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BFRAME_QP_DELTA,
        g_param_spec_int("bframe-qp-delta", "BFrame QP Delta",
                         "QP difference between BFrame QP and target QP, -1 = Disabled", MIN_BFRAME_QP_DELTA,
                         MAX_BFRAME_QP_DELTA, (gint)DEFAULT_BFRAME_QP_DELTA,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BITRATE,
        g_param_spec_uint("bitrate", "Target bitrate", "Target bitrate for rate control in bits/second", MIN_BITRATE,
                          MAX_BITRATE, (guint)DEFAULT_BITRATE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_TOL_MOVING_BITRATE,
        g_param_spec_uint("tol-moving-bitrate", "Tolerance moving bitrate",
                          "Percent tolerance over target bitrate of moving bit rate", MIN_BITRATE_VARIABLE_RANGE,
                          MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_TOL_MOVING_BITRATE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BITRATE_VAR_RANGE_I,
        g_param_spec_uint("bitvar-range-i", "Bitrate percent variation I frame",
                          "Percent variations over average bits per frame for I frame", MIN_BITRATE_VARIABLE_RANGE,
                          MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_BITVAR_RANGE_I,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BITRATE_VAR_RANGE_P,
        g_param_spec_uint("bitvar-range-p", "Bitrate percent variation P frame",
                          "Percent variations over average bits per frame for P frame", MIN_BITRATE_VARIABLE_RANGE,
                          MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_BITVAR_RANGE_P,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BITRATE_VAR_RANGE_B,
        g_param_spec_uint("bitvar-range-b", "Bitrate percent variation B frame",
                          "Percent variations over average bits per frame for B frame", MIN_BITRATE_VARIABLE_RANGE,
                          MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_BITVAR_RANGE_B,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_PICTURE_RC,
        g_param_spec_boolean("picture-rc", "Picture Rate Control", "Adjust QP between pictures", TRUE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_CTB_RC,
        g_param_spec_boolean("ctb-rc", "Block Rate Control", "Adaptive adjustment of QP inside frame", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_PICTURE_SKIP,
        g_param_spec_boolean("picture-skip", "Picture Skip", "Allow rate control to skip pictures", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_HRD,
        g_param_spec_boolean("hrd", "Picture Rate Control",
                             "Restricts the instantaneous bitrate and total bit amount of every coded picture.", false,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_CVBR,
        g_param_spec_uint("cvbr", "Picture Rate Control", "Rate control mode, makes VBR more like CBR.", MIN_CVBR_MODE,
                          MAX_CVBR_MODE, (guint)DEFAULT_CVBR_MODE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_PADDING,
        g_param_spec_boolean("padding", "Picture Rate Control", "Add padding to buffers on RC underflow.", false,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_MONITOR_FRAMES,
        g_param_spec_uint("monitor-frames", "Monitor Frames",
                          "How many frames will be monitored for moving bit rate. Default is using framerate",
                          AUTO_MONITOR_FRAMES, MAX_MONITOR_FRAMES, (gint)DEFAULT_MONITOR_FRAMES,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_ROI_AREA_1,
        g_param_spec_string("roi-area1", "ROI Area and QP Delta",
                            "Specifying rectangular area of CTBs as Region Of Interest with lower QP, "
                            "left:top:right:bottom:delta_qp format ",
                            NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_ROI_AREA_2,
        g_param_spec_string("roi-area2", "ROI Area and QP Delta",
                            "Specifying rectangular area of CTBs as Region Of Interest with lower QP, "
                            "left:top:right:bottom:delta_qp format ",
                            NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_COMPRESSOR,
                                    g_param_spec_enum("compressor", "Compressor", "Enable/Disable Embedded Compression",
                                                      GST_TYPE_HAILOENC_COMPRESSOR, (guint)3,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_BLOCK_RC_SIZE,
        g_param_spec_enum("block-rc-size", "Block Rate Control Size", "Size of block rate control",
                          GST_TYPE_HAILOENC_BLOCK_RC_SIZE, (guint)0,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_HRD_CPB_SIZE,
        g_param_spec_uint("hrd-cpb-size", "HRD Coded Picture Buffer size", "Buffer size used by the HRD model in bits",
                          MIN_HRD_CPB_SIZE, MAX_HRD_CPB_SIZE, (guint)DEFAULT_HRD_CPB_SIZE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_ADAPT_FRAMERATE,
        g_param_spec_boolean("adapt-framerate", "Adapt Framerate", "Adapt encoder to real framerate", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_FRAMERATE_TOLERANCE,
        g_param_spec_uint("framerate-tolerance", "Framerate Tolerance",
                          "Framerate tolerance in percent. Relevant only if adapt-framerate is enabled",
                          MIN_FRAMERATE_TOLERANCE, MAX_FRAMERATE_TOLERANCE, (guint)DEFAULT_FRAMERATE_TOLERANCE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    /* Smooth bitrate adjustment parameters */
    g_object_class_install_property(
        gobject_class, PROP_ENABLE_GOP_BITRATE_ADJUSTER,
        g_param_spec_boolean("gop-anomaly-bitrate-adjuster-enable", "Enable GOP Anomaly Bitrate Adjuster",
                             "Enable/disable gop anomaly bitrate adjuster", DEFAULT_ENABLE_GOP_BITRATE_ADJUSTER,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_THRESHOLD_HIGH,
        g_param_spec_float("gop-anomaly-bitrate-adjuster-high-threshold", "High Threshold",
                           "High threshold for GOP frame analysis", 0.0, 1.0, DEFAULT_THRESHOLD_HIGH,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_THRESHOLD_LOW,
        g_param_spec_float("gop-anomaly-bitrate-adjuster-low-threshold", "Low Threshold",
                           "Low threshold for GOP frame analysis", 0.0, 1.0, DEFAULT_THRESHOLD_LOW,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_MAX_TARGET_BITRATE_FACTOR,
        g_param_spec_float("gop-anomaly-bitrate-adjuster-max-factor", "Max Target Bitrate Factor",
                           "Maximum target bitrate factor", 1.0, 10.0, DEFAULT_MAX_TARGET_BITRATE_FACTOR,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BITRATE_ADJUSTMENT_FACTOR,
        g_param_spec_float("gop-anomaly-bitrate-adjuster-factor", "Bitrate Adjustment Factor",
                           "Bitrate adjustment factor", 0.0, 1.0, DEFAULT_BITRATE_ADJUSTMENT_FACTOR,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    /* QP smooth settings */
    g_object_class_install_property(
        gobject_class, PROP_QP_SMOOTH_QP_DELTA,
        g_param_spec_int("smooth-qp-delta", "Smooth QP Delta", "smooth QP delta parameter", 0, 300,
                         DEFAULT_QP_SMOOTH_QP_DELTA,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QP_SMOOTH_QP_DELTA_LIMIT,
        g_param_spec_int("smooth-qp-delta-limit", "Smooth QP Delta Limit", "Smooth QP delta limit parameter", 0, 4000,
                         DEFAULT_QP_SMOOTH_QP_DELTA_LIMIT,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QP_SMOOTH_QP_DELTA_INCREMENT,
        g_param_spec_uint("smooth-qp-delta-step", "Smooth QP Delta step", "smooth QP delta step parameter", 0, 300,
                          DEFAULT_QP_SMOOTH_QP_DELTA_INCREMENT,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QP_SMOOTH_QP_DELTA_LIMIT_INCREMENT,
        g_param_spec_uint("smooth-qp-delta-limit-step", "Smooth QP Delta Limit step",
                          "smooth QP delta limit step parameter", 0, 1000, DEFAULT_QP_SMOOTH_QP_DELTA_LIMIT_INCREMENT,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QP_SMOOTH_QP_ALPHA,
        g_param_spec_float("smooth-qp-alpha", "Smooth QP Alpha", "smooth alpha parameter", 0.0, 1.0,
                           DEFAULT_QP_SMOOTH_QP_ALPHA,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_QP_SMOOTH_Q_STEP_DIVISOR,
        g_param_spec_int("smooth-qp-step-divisor", "Smooth Qp Step divisor ", "smooth Qp step divisor parameter", 1, 5,
                         2, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    /* Boost parameters */
    g_object_class_install_property(
        gobject_class, PROP_BOOST_ENABLED,
        g_param_spec_boolean("zoom-bitrate-adjuster-zooming-enable", "Enable zoom bitrate adjuster",
                             "Enable/disable zoom bitrate adjuster for optical zoom", DEFAULT_BOOST_ENABLED,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BOOST_FACTOR,
        g_param_spec_float("zoom-bitrate-adjuster-zooming-bitrate-factor", "Boost Factor",
                           "Bitrate adjustment factor for optical zoom", 1.0, 10.0, DEFAULT_BOOST_FACTOR,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BOOST_TIMEOUT_MS,
        g_param_spec_uint("zoom-bitrate-adjuster-zooming-timeout-ms", "Boost Timeout",
                          "Zoom bitrate adjust timeout in milliseconds", 0, 60000, DEFAULT_BOOST_TIMEOUT_MS,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_BOOST_MAX_BITRATE,
        g_param_spec_uint("zoom-bitrate-adjuster-zooming-max-bitrate", "Boost Max Bitrate",
                          "Maximum bitrate when adjusting in optical zoom (0 = no limit)", 0, 400000000,
                          DEFAULT_BOOST_MAX_BITRATE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_FORCE_KEYFRAME_ON_ZOOM,
        g_param_spec_boolean("zoom-bitrate-adjuster-zooming-force-keyframe", "Force Keyframe on Zoom",
                             "Force keyframe when optical zoom changes", DEFAULT_FORCE_KEYFRAME_ON_ZOOM,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    /* Constant optical zoom boost parameters */
    g_object_class_install_property(
        gobject_class, PROP_CONSTANT_OPTICAL_ZOOM_BOOST,
        g_param_spec_boolean("zoom-bitrate-adjuster-zoom-level-enable", "Constant Optical Zoom Boost",
                             "Enable/disable constant bitrate boost for high optical zoom levels",
                             DEFAULT_CONSTANT_OPTICAL_ZOOM_BOOST,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_CONSTANT_OPTICAL_ZOOM_BOOST_THRESHOLD,
        g_param_spec_float("zoom-bitrate-adjuster-zoom-level-threshold", "Constant Boost Threshold",
                           "Optical zoom level threshold for activating constant boost", 1.0, 20.0,
                           DEFAULT_CONSTANT_OPTICAL_ZOOM_BOOST_THRESHOLD,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_CONSTANT_OPTICAL_ZOOM_BOOST_FACTOR,
        g_param_spec_float("zoom-bitrate-adjuster-zoom-level-bitrate-factor", "Constant Boost Factor",
                           "Bitrate boost factor for constant optical zoom boost", 1.0, 10.0,
                           DEFAULT_CONSTANT_OPTICAL_ZOOM_BOOST_FACTOR,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    venc_class->start = gst_hailoenc_start;
    venc_class->stop = gst_hailoenc_stop;
    venc_class->finish = gst_hailoenc_finish;
    venc_class->handle_frame = gst_hailoenc_handle_frame;
    venc_class->set_format = gst_hailoenc_set_format;
    venc_class->propose_allocation = gst_hailoenc_propose_allocation;
    venc_class->flush = gst_hailoenc_flush;

    gobject_class->finalize = gst_hailoenc_finalize;
    gobject_class->dispose = gst_hailoenc_dispose;
}

static void gst_hailoenc_init(GstHailoEnc *hailoenc)
{
    hailoenc->params = new GstHailoEncParams();

    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    hailoenc->params->apiVer = VCEncGetApiVersion();
    hailoenc->params->encBuild = VCEncGetBuild();
    hailoenc->params->stream_restart = FALSE;
    memset(enc_params, 0, sizeof(EncoderParams));
    memset(hailoenc->params->gopPicCfg, 0, sizeof(hailoenc->params->gopPicCfg));
    hailoenc->params->encoder_instance = NULL;
    enc_params->encIn.gopConfig.pGopPicCfg = hailoenc->params->gopPicCfg;
    hailoenc->params->adapt_framerate = FALSE;
    hailoenc->params->is_user_set_bitrate = FALSE;
    hailoenc->params->framerate_tolerance = 1.15f;
    hailoenc->params->dts_queue = g_queue_new();
    g_queue_init(hailoenc->params->dts_queue);

    /* Initialize boost mechanism state */
    hailoenc->params->zooming_boost_enabled = false;
    hailoenc->params->original_bitrate = 0;
    hailoenc->params->original_gop_anomaly_bitrate_adjuster_enable = false;
    hailoenc->params->settings_boost_start_time_ns = 0;
    hailoenc->params->previous_optical_zoom_magnification = 1.0f;
}

/************************
GObject Virtual Functions
************************/

static void gst_hailoenc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)(object);

    switch (prop_id)
    {
    case PROP_INTRA_PIC_RATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.intraPicRate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.gopSize);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_LENGTH:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.gopLength);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPHDR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, (gint)hailoenc->params->enc_params.qphdr);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMIN:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.qpmin);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMAX:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.qpmax);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_INTRA_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, (gint)hailoenc->params->enc_params.intra_qp_delta);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FIXED_INTRA_QP:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.fixed_intra_qp);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BFRAME_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, (gint)hailoenc->params->enc_params.bFrameQpDelta);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.bitrate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_TOL_MOVING_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.tolMovingBitRate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_I:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.bitVarRangeI);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_P:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.bitVarRangeP);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_B:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.bitVarRangeB);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_MONITOR_FRAMES:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.monitorFrames);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PICTURE_RC: {
        GST_OBJECT_LOCK(hailoenc);
        gboolean picture_rc = hailoenc->params->enc_params.pictureRc == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, picture_rc);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_CTB_RC: {
        GST_OBJECT_LOCK(hailoenc);
        gboolean ctb_rc = hailoenc->params->enc_params.ctbRc == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, ctb_rc);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_PICTURE_SKIP: {
        GST_OBJECT_LOCK(hailoenc);
        gboolean picture_skip = hailoenc->params->enc_params.pictureSkip == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, picture_skip);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_HRD: {
        GST_OBJECT_LOCK(hailoenc);
        gboolean hrd = hailoenc->params->enc_params.hrd == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, hrd);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_CVBR: {
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.cvbr);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_PADDING: {
        GST_OBJECT_LOCK(hailoenc);
        gboolean padding = hailoenc->params->enc_params.padding == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, padding);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_ROI_AREA_1:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_string(value, (const gchar *)hailoenc->params->enc_params.roiArea1);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ROI_AREA_2:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_string(value, (const gchar *)hailoenc->params->enc_params.roiArea2);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_COMPRESSOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_enum(value, (gint)hailoenc->params->enc_params.compressor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BLOCK_RC_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_enum(value, (gint)hailoenc->params->enc_params.blockRcSize);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_HRD_CPB_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->params->enc_params.hrdCpbSize);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ADAPT_FRAMERATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_boolean(value, hailoenc->params->adapt_framerate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FRAMERATE_TOLERANCE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)((hailoenc->params->framerate_tolerance - 1.0f) * 100.0f));
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ENABLE_GOP_BITRATE_ADJUSTER:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_boolean(value, hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_enable);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_THRESHOLD_HIGH:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_high_threshold);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_THRESHOLD_LOW:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_low_threshold);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_MAX_TARGET_BITRATE_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_max_factor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_ADJUSTMENT_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_factor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, hailoenc->params->enc_params.qp_smooth_qp_delta);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA_LIMIT:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, hailoenc->params->enc_params.qp_smooth_qp_delta_limit);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA_INCREMENT:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, hailoenc->params->enc_params.qp_smooth_qp_delta_step);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA_LIMIT_INCREMENT:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, hailoenc->params->enc_params.qp_smooth_qp_delta_limit_step);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_ALPHA:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.qp_smooth_qp_alpha);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_Q_STEP_DIVISOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, hailoenc->params->enc_params.qp_smooth_q_step_divisor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_ENABLED:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_boolean(value, hailoenc->params->enc_params.zoom_bitrate_adjuster_enable);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.zoom_bitrate_adjuster_factor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_TIMEOUT_MS:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, hailoenc->params->enc_params.zoom_bitrate_adjuster_timeout_ms);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_MAX_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, hailoenc->params->enc_params.zoom_bitrate_adjuster_max_bitrate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FORCE_KEYFRAME_ON_ZOOM:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_boolean(value, hailoenc->params->enc_params.zoom_bitrate_adjuster_force_keyframe);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CONSTANT_OPTICAL_ZOOM_BOOST:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_boolean(value, hailoenc->params->enc_params.constant_optical_zoom_boost);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CONSTANT_OPTICAL_ZOOM_BOOST_THRESHOLD:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.constant_optical_zoom_boost_threshold);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CONSTANT_OPTICAL_ZOOM_BOOST_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_float(value, hailoenc->params->enc_params.constant_optical_zoom_boost_factor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_hailoenc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)(object);
    hailoenc->params->update_config = hailoenc->params->enc_params.picture_enc_cnt != 0;

    switch (prop_id)
    {
    case PROP_INTRA_PIC_RATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.intraPicRate = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gopSize = g_value_get_uint(value);
        if (hailoenc->params->enc_params.gopSize > MAX_GOP_SIZE)
        {
            GST_WARNING_OBJECT(hailoenc, "GOP size %d is too large, setting to max %d",
                               hailoenc->params->enc_params.gopSize, MAX_GOP_SIZE);
            hailoenc->params->enc_params.gopSize = MAX_GOP_SIZE;
        }
        else if (hailoenc->params->enc_params.gopSize < MIN_GOP_SIZE)
        {
            GST_WARNING_OBJECT(hailoenc, "GOP size %d is too small, setting to min %d",
                               hailoenc->params->enc_params.gopSize, MIN_GOP_SIZE);
            hailoenc->params->enc_params.gopSize = MIN_GOP_SIZE;
        }
        hailoenc->params->update_gop_size = TRUE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_LENGTH:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gopLength = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPHDR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qphdr = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMIN:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qpmin = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMAX:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qpmax = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_INTRA_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.intra_qp_delta = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FIXED_INTRA_QP:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.fixed_intra_qp = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BFRAME_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.bFrameQpDelta = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.bitrate = g_value_get_uint(value);
        hailoenc->params->is_user_set_bitrate = TRUE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_TOL_MOVING_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.tolMovingBitRate = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_I:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.bitVarRangeI = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_P:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.bitVarRangeP = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_B:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.bitVarRangeB = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_MONITOR_FRAMES:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.monitorFrames = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PICTURE_RC:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.pictureRc = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CTB_RC:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.ctbRc = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PICTURE_SKIP:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.pictureSkip = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_HRD:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.hrd = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CVBR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.cvbr = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PADDING:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.padding = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ROI_AREA_1:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.roiArea1 = (char *)g_value_get_string(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ROI_AREA_2:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.roiArea2 = (char *)g_value_get_string(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_COMPRESSOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.compressor = (u32)g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BLOCK_RC_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.blockRcSize = (u32)g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_HRD_CPB_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.hrdCpbSize = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ADAPT_FRAMERATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->adapt_framerate = g_value_get_boolean(value);
        hailoenc->params->update_config = FALSE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FRAMERATE_TOLERANCE:
        GST_OBJECT_LOCK(hailoenc);
        GST_WARNING_OBJECT(hailoenc, "Setting framerate tolerance to %d", g_value_get_uint(value));
        hailoenc->params->framerate_tolerance = (float)((float)g_value_get_uint(value) / 100.0f + 1.0f);
        hailoenc->params->update_config = FALSE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ENABLE_GOP_BITRATE_ADJUSTER:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_enable = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_THRESHOLD_HIGH:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_high_threshold = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_THRESHOLD_LOW:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_low_threshold = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_MAX_TARGET_BITRATE_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_max_factor = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_ADJUSTMENT_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.gop_anomaly_bitrate_adjuster_factor = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qp_smooth_qp_delta = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA_LIMIT:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qp_smooth_qp_delta_limit = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA_INCREMENT:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qp_smooth_qp_delta_step = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_DELTA_LIMIT_INCREMENT:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qp_smooth_qp_delta_limit_step = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_QP_ALPHA:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qp_smooth_qp_alpha = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QP_SMOOTH_Q_STEP_DIVISOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.qp_smooth_q_step_divisor = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_ENABLED:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.zoom_bitrate_adjuster_enable = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.zoom_bitrate_adjuster_factor = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_TIMEOUT_MS:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.zoom_bitrate_adjuster_timeout_ms = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BOOST_MAX_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.zoom_bitrate_adjuster_max_bitrate = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FORCE_KEYFRAME_ON_ZOOM:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.zoom_bitrate_adjuster_force_keyframe = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CONSTANT_OPTICAL_ZOOM_BOOST:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.constant_optical_zoom_boost = g_value_get_boolean(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CONSTANT_OPTICAL_ZOOM_BOOST_THRESHOLD:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.constant_optical_zoom_boost_threshold = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CONSTANT_OPTICAL_ZOOM_BOOST_FACTOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->params->enc_params.constant_optical_zoom_boost_factor = g_value_get_float(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    default:
        hailoenc->params->update_config = FALSE;
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_hailoenc_finalize(GObject *object)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)object;
    GST_DEBUG_OBJECT(hailoenc, "hailoenc finalize callback");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_hailoenc_dispose(GObject *object)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)object;
    GST_DEBUG_OBJECT(hailoenc, "hailoenc dispose callback");

    /* clean up as possible.  may be called multiple times */
    if (hailoenc->params != nullptr)
    {
        delete hailoenc->params;
        hailoenc->params = nullptr;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*****************
Internal Functions
*****************/

/**
 * Get current time in nanoseconds
 */
static uint64_t get_current_time_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * Check if boost timeout has elapsed and restore settings if needed
 */
static void check_and_restore_boost_settings(GstHailoEnc *hailoenc, float current_optical_zoom)
{
    EncoderParams &enc_params = hailoenc->params->enc_params;

    if ((hailoenc->params->encoder_instance == NULL) || (!hailoenc->params->zooming_boost_enabled))
    {
        return;
    }

    uint64_t current_time_ns = get_current_time_ns();
    uint64_t elapsed_ms = (current_time_ns - hailoenc->params->settings_boost_start_time_ns) / 1000000ULL;

    if (elapsed_ms >= enc_params.zoom_bitrate_adjuster_timeout_ms)
    {
        // Restore original settings - don't modify enc_params.bitrate as it should always be the baseline
        enc_params.gop_anomaly_bitrate_adjuster_enable = hailoenc->params->original_gop_anomaly_bitrate_adjuster_enable;
        hailoenc->params->zooming_boost_enabled = false;

        GST_INFO_OBJECT(hailoenc, "Temporary boost timeout after %lu ms", (unsigned long)elapsed_ms);

        // Determine what bitrate to set in the encoder
        u32 target_encoder_bitrate = enc_params.bitrate; // Start with baseline

        // Check if constant optical zoom boost should be applied instead
        if (enc_params.constant_optical_zoom_boost &&
            current_optical_zoom >= enc_params.constant_optical_zoom_boost_threshold)
        {
            // Apply constant boost to the baseline bitrate
            target_encoder_bitrate = (u32)(enc_params.bitrate * enc_params.constant_optical_zoom_boost_factor);

            GST_INFO_OBJECT(hailoenc,
                            "Applying constant optical zoom boost after temporary boost timeout: baseline %u -> "
                            "encoder %u (zoom: %.1fx)",
                            enc_params.bitrate, target_encoder_bitrate, current_optical_zoom);
        }
        else
        {
            GST_INFO_OBJECT(hailoenc,
                            "Restored to baseline bitrate %u after timeout (constant_optical_zoom_boost: %s, "
                            "current_optical_zoom: %.1fx, threshold: %.1fx)",
                            target_encoder_bitrate, enc_params.constant_optical_zoom_boost ? "enabled" : "disabled",
                            current_optical_zoom, enc_params.constant_optical_zoom_boost_threshold);
        }

        // Update encoder rate control if encoder is running
        VCEncRateCtrl rcCfg;
        VCEncRet ret = VCEncGetRateCtrl(hailoenc->params->encoder_instance, &rcCfg);
        if (ret == VCENC_OK)
        {
            rcCfg.bitPerSecond = target_encoder_bitrate;
            ret = VCEncSetRateCtrl(hailoenc->params->encoder_instance, &rcCfg);
            if (ret != VCENC_OK)
            {
                GST_ERROR_OBJECT(hailoenc, "Failed to set bitrate after boost timeout, error: %d", ret);
            }
        }

        // Force keyframe if requested
        if (enc_params.zoom_bitrate_adjuster_force_keyframe)
        {
            ForceKeyframe(&enc_params);
            GST_DEBUG_OBJECT(hailoenc, "Forced keyframe after optical zoom timeout");
        }
    }
}

/**
 * Apply boost settings when optical zoom is detected
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] optical_zoom_magnification The optical zoom magnification factor.
 */
static void boost_settings_for_optical_zoom(GstHailoEnc *hailoenc, float optical_zoom_magnification)
{
    EncoderParams *enc_params = &(hailoenc->params->enc_params);

    if (!enc_params->zoom_bitrate_adjuster_enable)
    {
        return;
    }

    if (!hailoenc->params->zooming_boost_enabled)
    {
        // Use the baseline bitrate from enc_params, which should not be affected by constant boost
        // since constant boost only modifies the encoder's rate control directly
        u32 baseline_bitrate = enc_params->bitrate;
        u32 boosted_bitrate = (u32)(baseline_bitrate * enc_params->zoom_bitrate_adjuster_factor);

        // Apply max_bitrate limit if set (0 means no limit)
        if (enc_params->zoom_bitrate_adjuster_max_bitrate > 0 &&
            boosted_bitrate > enc_params->zoom_bitrate_adjuster_max_bitrate)
        {
            boosted_bitrate = enc_params->zoom_bitrate_adjuster_max_bitrate;
        }

        // Store original baseline values (not current potentially boosted values)
        hailoenc->params->original_bitrate = baseline_bitrate;
        hailoenc->params->original_gop_anomaly_bitrate_adjuster_enable =
            enc_params->gop_anomaly_bitrate_adjuster_enable;

        // Apply boost - never modify enc_params->bitrate as it should always be the baseline
        enc_params->gop_anomaly_bitrate_adjuster_enable = false; // Disable smooth bitrate during boost
        hailoenc->params->zooming_boost_enabled = true;

        GST_INFO_OBJECT(hailoenc, "Boosted bitrate from %u to %u (factor: %.1f, max: %u) due to optical zoom %.1fx",
                        baseline_bitrate, boosted_bitrate, enc_params->zoom_bitrate_adjuster_factor,
                        enc_params->zoom_bitrate_adjuster_max_bitrate, optical_zoom_magnification);

        // Update encoder rate control if encoder is running
        if (hailoenc->params->encoder_instance != NULL)
        {
            VCEncRateCtrl rcCfg;
            VCEncRet ret = VCEncGetRateCtrl(hailoenc->params->encoder_instance, &rcCfg);
            if (ret != VCENC_OK)
            {
                GST_WARNING_OBJECT(hailoenc, "Failed to get rate control for optical zoom boost, error: %d", ret);
            }

            if (rcCfg.bitPerSecond != boosted_bitrate)
            {
                rcCfg.bitPerSecond = boosted_bitrate;
                ret = VCEncSetRateCtrl(hailoenc->params->encoder_instance, &rcCfg);
                if (ret != VCENC_OK)
                {
                    GST_ERROR_OBJECT(hailoenc, "Failed to set boosted bitrate, error: %d", ret);
                }
            }
        }

        // Force keyframe if requested
        if (enc_params->zoom_bitrate_adjuster_force_keyframe)
        {
            ForceKeyframe(enc_params);
            GST_DEBUG_OBJECT(hailoenc, "Forced keyframe due to optical zoom change");
        }
    }

    // Reset or start the timer
    hailoenc->params->settings_boost_start_time_ns = get_current_time_ns();
}

/**
 * Apply constant boost settings based on optical zoom level when threshold is exceeded
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] optical_zoom_magnification The optical zoom magnification factor.
 */
static void apply_constant_optical_zoom_boost(GstHailoEnc *hailoenc, float optical_zoom_magnification)
{
    EncoderParams *enc_params = &(hailoenc->params->enc_params);

    // Only apply constant boost if enabled and original boost is not active
    if (!enc_params->constant_optical_zoom_boost || hailoenc->params->zooming_boost_enabled)
    {
        return;
    }

    // Update encoder rate control if encoder is running
    if (hailoenc->params->encoder_instance != NULL)
    {
        VCEncRateCtrl rcCfg;
        u32 current_bitrate = enc_params->bitrate;
        u32 boosted_bitrate = (u32)(current_bitrate * enc_params->constant_optical_zoom_boost_factor);

        VCEncRet ret = VCEncGetRateCtrl(hailoenc->params->encoder_instance, &rcCfg);
        if (ret != VCENC_OK)
        {
            GST_WARNING_OBJECT(hailoenc, "Failed to get rate control for constant optical zoom boost, error: %d", ret);
            return;
        }

        // Check if current zoom level exceeds threshold
        if (optical_zoom_magnification < enc_params->constant_optical_zoom_boost_threshold)
        {
            // Zoom level is below threshold, ensure constant boost is not applied
            GST_DEBUG_OBJECT(hailoenc, "Optical zoom %.1fx is below constant boost threshold %.1f",
                             optical_zoom_magnification, enc_params->constant_optical_zoom_boost_threshold);
            boosted_bitrate = current_bitrate; // No boost
        }

        if (rcCfg.bitPerSecond != boosted_bitrate) // Only apply if different from current
        {
            rcCfg.bitPerSecond = boosted_bitrate;
            ret = VCEncSetRateCtrl(hailoenc->params->encoder_instance, &rcCfg);
            if (ret != VCENC_OK)
            {
                GST_ERROR_OBJECT(hailoenc, "Failed to set constant optical zoom boost bitrate, error: %d", ret);
            }
            else
            {
                GST_DEBUG_OBJECT(hailoenc,
                                 "Applied constant optical zoom boost: bitrate %u -> %u (factor: %.1f) for zoom %.1fx",
                                 current_bitrate, boosted_bitrate, enc_params->constant_optical_zoom_boost_factor,
                                 optical_zoom_magnification);
            }
        }
    }
}

/**
 * Updates the encoder with the input video info.
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] info         A GstVideoInfo object containing the input video info for this pipeline.
 * @returns TRUE if the encoder parameters were updated, FALSE otherwise.
 * @note The updated data is the resolution, framerate and input format.
 */
static gboolean gst_hailoenc_update_params(GstHailoEnc *hailoenc, GstVideoInfo *info)
{
    gboolean updated_params = FALSE;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);

    if (enc_params->width != GST_VIDEO_INFO_WIDTH(info) || enc_params->height != GST_VIDEO_INFO_HEIGHT(info))
    {
        enc_params->width = GST_VIDEO_INFO_WIDTH(info);
        enc_params->stride = GST_VIDEO_INFO_PLANE_STRIDE(info, 0);
        enc_params->height = GST_VIDEO_INFO_HEIGHT(info);
        updated_params = TRUE;
    }

    if (enc_params->frameRateNumer != GST_VIDEO_INFO_FPS_N(info) ||
        enc_params->frameRateDenom != GST_VIDEO_INFO_FPS_D(info))
    {
        enc_params->frameRateNumer = GST_VIDEO_INFO_FPS_N(info);
        enc_params->frameRateDenom = GST_VIDEO_INFO_FPS_D(info);
        updated_params = TRUE;
    }

    switch (GST_VIDEO_INFO_FORMAT(info))
    {
    case GST_VIDEO_FORMAT_NV12:
        enc_params->inputFormat = VCENC_YUV420_SEMIPLANAR;
        break;
    case GST_VIDEO_FORMAT_NV21:
        enc_params->inputFormat = VCENC_YUV420_SEMIPLANAR_VU;
        break;
    case GST_VIDEO_FORMAT_I420:
        enc_params->inputFormat = VCENC_YUV420_PLANAR;
        break;
    default:
        GST_ERROR_OBJECT(hailoenc, "Unsupported format %d", GST_VIDEO_INFO_FORMAT(info));
        break;
    }
    return updated_params;
}

/**
 * Updated the encoder parameters with the physical addresses of the current input buffer.
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] frame        The GstVideoCodecFrame object with the input GstBuffer inside.
 * @return GST_FLOW_OK on success, GST_FLOW_ERROR on failure.
 * @note The function will fail when it cannot get the physical address or the memory is non-continous.
 */
static GstFlowReturn gst_hailoenc_update_input_buffer(GstHailoEnc *hailoenc, HailoMediaLibraryBufferPtr hailo_buffer)
{
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    uint32_t *luma = nullptr;
    uint32_t *chroma = nullptr;
    int lumaFd = -1;
    int chromaFd = -1;
    size_t luma_size = 0;
    size_t chroma_size = 0;
    uint32_t stride = 0;
    int ewl_ret;

    if (!hailo_buffer)
    {
        GST_ERROR_OBJECT(hailoenc, "Null hailo buffer");
        return GST_FLOW_ERROR;
    }

    luma_size = hailo_buffer->get_plane_size(0);
    chroma_size = hailo_buffer->get_plane_size(1);
    stride = hailo_buffer->get_plane_stride(0);

    if (luma_size == 0 || chroma_size == 0)
    {
        GST_ERROR_OBJECT(hailoenc, "luma %p luma_size %zu chroma %p chroma_size %zu", luma, luma_size, chroma,
                         chroma_size);
        return GST_FLOW_ERROR;
    }

    if (stride != enc_params->stride)
    {
        GST_WARNING_OBJECT(hailoenc, "Stride changed from %u to %u", enc_params->stride, stride);
        enc_params->stride = stride;
        InitEncoderPreProcConfig(enc_params, &(hailoenc->params->encoder_instance));
    }

    if (hailo_buffer->is_dmabuf())
    {
        lumaFd = hailo_buffer->get_plane_fd(0);
        chromaFd = hailo_buffer->get_plane_fd(1);
        if (lumaFd <= 0 || chromaFd <= 0)
        {
            GST_ERROR_OBJECT(hailoenc, "Could not get input dma buffer luma and chroma");
            return GST_FLOW_ERROR;
        }
        // Get the physical Addresses of input buffer luma and chroma.
        ewl_ret = EWLShareDmabuf(enc_params->ewl, lumaFd, &(enc_params->encIn.busLuma));
        if (ewl_ret != EWL_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Could not get physical address of input picture luma");
            return GST_FLOW_ERROR;
        }
        ewl_ret = EWLShareDmabuf(enc_params->ewl, chromaFd, &(enc_params->encIn.busChromaU));
        if (ewl_ret != EWL_OK)
        {
            EWLUnshareDmabuf(enc_params->ewl, lumaFd);
            GST_ERROR_OBJECT(hailoenc, "Could not get physical address of input picture chroma");
            return GST_FLOW_ERROR;
        }
    }
    else
    {
        luma = static_cast<uint32_t *>(hailo_buffer->get_plane_ptr(0));
        chroma = static_cast<uint32_t *>(hailo_buffer->get_plane_ptr(1));
        if (luma == nullptr || chroma == nullptr)
        {
            GST_ERROR_OBJECT(hailoenc, "Could not get input buffer luma and chroma");
            return GST_FLOW_ERROR;
        }

        // Get the physical Addresses of input buffer luma and chroma.
        ewl_ret = EWLGetBusAddress(enc_params->ewl, luma, &(enc_params->encIn.busLuma), luma_size);
        if (ewl_ret != EWL_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Could not get physical address of input picture luma");
            return GST_FLOW_ERROR;
        }
        ewl_ret = EWLGetBusAddress(enc_params->ewl, chroma, &(enc_params->encIn.busChromaU), chroma_size);
        if (ewl_ret != EWL_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Could not get physical address of input picture chroma");
            return GST_FLOW_ERROR;
        }
    }
    return GST_FLOW_OK;
}

/**
 * Creats a GstBuffer object with the encoded data as memory.
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @return A GstBuffer object of the encoded data.
 * @note It also modifies parameters containing the total streamed bytes and bits.
 */
static GstBuffer *gst_hailoenc_get_encoded_buffer(GstHailoEnc *hailoenc)
{
    GstBuffer *outbuf;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    outbuf = gst_buffer_new_memdup(enc_params->outbufMem.virtualAddress, enc_params->encOut.streamSize);
    return outbuf;
}

/**
 * Add headers to the encoded stream
 *
 * @param[in] hailoenc       The HailoEnc gstreamer instance.
 * @param[in] new_header     The new header as GstBuf fer object.
 */
static void gst_hailoenc_add_headers(GstHailoEnc *hailoenc, GstBuffer *new_header)
{
    if (hailoenc->params->header_buffer)
        hailoenc->params->header_buffer = gst_buffer_append(hailoenc->params->header_buffer, new_header);
    else
        hailoenc->params->header_buffer = new_header;
}

/**
 * Encode and set the header - Performed via VCEncStrmStart
 *
 * @param[in] encoder     The GstVideoEncoder object.
 * @return Upon success, returns VCENC_OK. Otherwise, returns another error value from VCEncRet.
 */
static VCEncRet gst_hailoenc_encode_header(GstVideoEncoder *encoder)
{
    VCEncRet enc_ret;
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    VCEncIn *pEncIn = &(enc_params->encIn);
    VCEncOut *pEncOut = &(enc_params->encOut);
    pEncIn->gopSize = enc_params->gopSize;

    if (hailoenc->params->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return VCENC_ERROR;
    }
    enc_ret = VCEncStrmStart(hailoenc->params->encoder_instance, pEncIn, pEncOut);
    if (enc_ret != VCENC_OK)
    {
        return enc_ret;
    }
    gst_hailoenc_add_headers(hailoenc, gst_hailoenc_get_encoded_buffer(hailoenc));

    // Default gop size as IPPP
    pEncIn->poc = 0;
    pEncIn->gopSize = enc_params->nextGopSize = ((enc_params->gopSize == 0) ? 1 : enc_params->gopSize);
    enc_params->nextCodingType = VCENC_INTRA_FRAME;

    return enc_ret;
}

/**
 * Restart the encoder
 *
 * @param[in] encoder     The GstVideoEncoder object.
 * @param[in] frame        A GstVideoCodecFrame used for sending stream_end data.
 * @return Upon success, returns GST_FLOW_OK, GST_FLOW_ERROR on failure.
 */
static GstFlowReturn gst_hailoenc_stream_restart(GstVideoEncoder *encoder)
{
    VCEncRet enc_ret;
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    VCEncIn *pEncIn = &(enc_params->encIn);
    VCEncOut *pEncOut = &(enc_params->encOut);
    GST_WARNING_OBJECT(hailoenc, "Restarting encoder");

    if (hailoenc->params->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return GST_FLOW_ERROR;
    }

    enc_ret = VCEncStrmEnd(hailoenc->params->encoder_instance, pEncIn, pEncOut);
    if (enc_ret != VCENC_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to end stream, returned %d", enc_ret);
        return GST_FLOW_ERROR;
    }

    if (enc_params->picture_enc_cnt == 0)
    {
        gst_buffer_unref(hailoenc->params->header_buffer);
        hailoenc->params->header_buffer = NULL;
    }

    if (hailoenc->params->hard_restart)
    {
        CloseEncoder(hailoenc->params->encoder_instance);
    }

    if (hailoenc->params->update_gop_size)
    {
        GST_DEBUG_OBJECT(hailoenc, "Updating gop size to %u", enc_params->gopSize);
        memset(hailoenc->params->gopPicCfg, 0, sizeof(hailoenc->params->gopPicCfg));
        memset(enc_params->gopCfgOffset, 0, sizeof(enc_params->gopCfgOffset));
        memset(&(enc_params->encIn.gopConfig), 0, sizeof(enc_params->encIn.gopConfig));
        enc_params->encIn.gopConfig.pGopPicCfg = hailoenc->params->gopPicCfg;
        if (VCEncInitGopConfigs(enc_params->gopSize, NULL, &(enc_params->encIn.gopConfig), enc_params->gopCfgOffset,
                                enc_params->bFrameQpDelta, enc_params->codecH264) != 0)
        {
            GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to update gop size");
            return GST_FLOW_ERROR;
        }
        hailoenc->params->update_gop_size = FALSE;
    }

    if (hailoenc->params->hard_restart)
    {
        GST_INFO_OBJECT(hailoenc, "Reopening encoder");
        if (OpenEncoder(&(hailoenc->params->encoder_instance), enc_params) != 0)
        {
            GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to reopen encoder");
            return GST_FLOW_ERROR;
        }
        hailoenc->params->hard_restart = FALSE;
    }
    else
    {
        if (UpdateEncoderConfig(&(hailoenc->params->encoder_instance), enc_params) != 0)
        {
            GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to update configuration");
            return GST_FLOW_ERROR;
        }
    }

    enc_ret = gst_hailoenc_encode_header(encoder);
    if (enc_ret != VCENC_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to encode headers, returned %d", enc_ret);
        return GST_FLOW_ERROR;
    }

    hailoenc->params->update_config = FALSE;
    hailoenc->params->stream_restart = FALSE;
    return GST_FLOW_OK;
}

/*
 * Send a slice to the downstream element

 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] address      The address of the slice.
 * @param[in] size         The size of the slice.
 * @return Upon success, returns GST_FLOW_OK, GST_FLOW_ERROR on failure.
 */
static GstFlowReturn gst_hailoenc_send_slice(GstHailoEnc *hailoenc, u32 *address, u32 size)
{
    GstBuffer *outbuf;
    GstVideoCodecFrame *frame;

    /* Get oldest frame */
    frame = gst_video_encoder_get_oldest_frame(GST_VIDEO_ENCODER(hailoenc));
    outbuf = gst_buffer_new_memdup(address, size);
    frame->output_buffer = outbuf;

    return gst_video_encoder_finish_subframe(GST_VIDEO_ENCODER(hailoenc), frame);
}

/*
 * Callback function for slice ready event
 *
 * @param[in] slice     The slice ready event.
 * @return void
 */
static void gst_hailoenc_slice_ready(VCEncSliceReady *slice)
{
    u32 i;
    u32 streamSize;
    u8 *strmPtr;
    GstHailoEnc *hailoenc = (GstHailoEnc *)slice->pAppData;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    /* Here is possible to implement low-latency streaming by
     * sending the complete slices before the whole frame is completed. */

    if (enc_params->multislice_encoding)
    {
        if (slice->slicesReadyPrev == 0) /* New frame */
        {
            strmPtr = (u8 *)slice->pOutBuf; /* Pointer to beginning of frame */
            streamSize = 0;
            for (i = 0; i < slice->nalUnitInfoNum + slice->slicesReady; i++)
            {
                streamSize += *(slice->sliceSizes + i);
            }
            gst_hailoenc_send_slice(hailoenc, (u32 *)strmPtr, streamSize);
        }
        else
        {
            strmPtr = (u8 *)enc_params->strmPtr; /* Here we store the slice pointer */
            streamSize = 0;
            for (i = (slice->nalUnitInfoNum + slice->slicesReadyPrev); i < slice->slicesReady + slice->nalUnitInfoNum;
                 i++)
            {
                streamSize += *(slice->sliceSizes + i);
            }
            gst_hailoenc_send_slice(hailoenc, (u32 *)strmPtr, streamSize);
        }
        strmPtr += streamSize;
        /* Store the slice pointer for next callback */
        enc_params->strmPtr = strmPtr;
    }
}

static GstFlowReturn releaseDmabuf(GstHailoEnc *hailoenc, int fd)
{
    if (EWLUnshareDmabuf(hailoenc->params->enc_params.ewl, fd) != EWL_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Could not unshare dmabuf");
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

static GstFlowReturn handle_frame_ready(GstHailoEnc *hailoenc, GstVideoCodecFrame *frame, std::vector<int> *planeFds,
                                        int num_planes, bool is_dmabuf, bool send_null_buffer)
{
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    GstBuffer *null_buffer;
    GstFlowReturn ret = GST_FLOW_OK;
    if (send_null_buffer)
    {

        /* restart with yuv of next frame for IDR or GOP start */
        if (enc_params->encIn.poc == 0 || enc_params->encIn.gopPicIdx == 0)
        {
            enc_params->picture_cnt++;
            enc_params->last_idr_picture_cnt++;
        }
        else
        {

            /* follow current GOP, handling frame skip in API */
            enc_params->nextCodingType = find_next_pic(enc_params);
        }

        null_buffer = gst_buffer_new();
        gst_buffer_set_size(null_buffer, 0);
    }

    frame->dts = GPOINTER_TO_UINT(g_queue_pop_head(hailoenc->params->dts_queue));
    frame->duration = GST_SECOND / enc_params->frameRateNumer * enc_params->frameRateDenom;

    if (send_null_buffer)
        frame->output_buffer = null_buffer;
    else
        frame->output_buffer = gst_hailoenc_get_encoded_buffer(hailoenc);

    if (hailoenc->params->header_buffer)
    {
        frame->output_buffer = gst_buffer_append(hailoenc->params->header_buffer, frame->output_buffer);
        hailoenc->params->header_buffer = NULL;
    }
    if (GST_PAD_IS_FLUSHING(GST_VIDEO_ENCODER_SRC_PAD(hailoenc)))
    {
        GST_WARNING_OBJECT(hailoenc, "Pad is flushing, not sending frame %d", enc_params->picture_cnt);
        gst_buffer_unref(frame->output_buffer);
        frame->output_buffer = nullptr;
    }

    ret = gst_video_encoder_finish_frame(GST_VIDEO_ENCODER(hailoenc), frame);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Could not send encoded buffer, reason %d", ret);
        if (is_dmabuf)
        {
            for (int32_t i = 0; i < num_planes; i++)
                releaseDmabuf(hailoenc, (*planeFds)[i]);
        }
    }

    return ret;
}

/**
 * Encode a single frame
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] frame        A GstVideoCodecFrame containing the input to encode.
 * @return Upon success, returns GST_FLOW_OK, GST_FLOW_ERROR on failure.
 */
static GstFlowReturn encode_single_frame(GstHailoEnc *hailoenc, GstVideoCodecFrame *frame)
{
    GstFlowReturn ret = GST_FLOW_ERROR;
    VCEncRet enc_ret;
    HailoMediaLibraryBufferPtr hailo_buffer;
    bool is_dmabuf = false;
    int num_planes = 0;
    std::vector<int> planeFds;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    struct timespec start_encode, end_encode;
    GST_DEBUG_OBJECT(hailoenc, "Encoding frame number %u in type %u", frame->system_frame_number,
                     enc_params->nextCodingType);

    if (hailoenc->params->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return GST_FLOW_ERROR;
    }
    hailo_buffer = hailo_buffer_from_gst_buffer(frame->input_buffer, hailoenc->params->input_state->caps);
    if (!hailo_buffer)
    {
        GST_ERROR_OBJECT(hailoenc, "Could not get hailo buffer");
        return GST_FLOW_ERROR;
    }

    /* Apply boost settings based on optical zoom change */
    float current_optical_zoom = hailo_buffer->optical_zoom_magnification;

    if (!hailoenc->params->is_user_set_bitrate)
    {
        /* Check if we need to restore settings after timeout */
        check_and_restore_boost_settings(hailoenc, current_optical_zoom);

        GST_DEBUG_OBJECT(hailoenc, "Current optical zoom magnification: %.2f and previous magnification: %.2f",
                         current_optical_zoom, hailoenc->params->previous_optical_zoom_magnification);

        if (current_optical_zoom != hailoenc->params->previous_optical_zoom_magnification)
        {
            GST_INFO_OBJECT(hailoenc, "Optical zoom magnification changed from %.2f to %.2f",
                            hailoenc->params->previous_optical_zoom_magnification, current_optical_zoom);
            hailoenc->params->previous_optical_zoom_magnification = current_optical_zoom;

            boost_settings_for_optical_zoom(hailoenc, current_optical_zoom);
            /* Apply constant optical zoom boost if enabled and threshold is exceeded */
            apply_constant_optical_zoom_boost(hailoenc, current_optical_zoom);
        }
    }

    if (hailo_buffer->is_dmabuf())
    {
        is_dmabuf = true;
        num_planes = hailo_buffer->get_num_of_planes();
        planeFds.resize(num_planes);
        for (int32_t i = 0; i < num_planes; i++)
        {
            planeFds[i] = hailo_buffer->get_plane_fd(i);
            if (planeFds[i] <= 0)
            {
                GST_ERROR_OBJECT(hailoenc, "Could not get dmabuf fd of plane %d", i);
                return GST_FLOW_ERROR;
            }
        }
    }

    ret = gst_hailoenc_update_input_buffer(hailoenc, hailo_buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Could not update the input buffer");
        return ret;
    }

    clock_gettime(CLOCK_MONOTONIC, &start_encode);
    enc_ret = EncodeFrame(enc_params, hailoenc->params->encoder_instance, &gst_hailoenc_slice_ready, hailoenc);
    clock_gettime(CLOCK_MONOTONIC, &end_encode);
    GST_DEBUG_OBJECT(hailoenc, "Encode took %lu milliseconds",
                     (long)media_library_difftimespec_ms(end_encode, start_encode));
    GST_DEBUG_OBJECT(hailoenc, "Encode performance is %d cycles",
                     VCEncGetPerformance(hailoenc->params->encoder_instance));

    if (enc_ret == VCENC_HW_TIMEOUT)
    {
        GST_ERROR_OBJECT(hailoenc,
                         "Encode frame returned hardware timeout - Sending empty frame and restarting encoder sw");

        hailoenc->params->stream_restart = TRUE;
        hailoenc->params->hard_restart = TRUE;
    }

    switch (enc_ret)
    {
    case VCENC_HW_TIMEOUT:
    case VCENC_FRAME_READY:
        enc_params->picture_enc_cnt++;
        if (enc_params->encOut.streamSize == 0)
        {
            ret = GST_FLOW_OK;
            if (!hailoenc->params->enc_params.hrd && !hailoenc->params->enc_params.pictureSkip)
            {
                GST_WARNING_OBJECT(hailoenc, "Encoder didn't return any output for frame %d", enc_params->picture_cnt);
            }

            ret = handle_frame_ready(hailoenc, frame, &planeFds, num_planes, is_dmabuf, true);
            if (ret != GST_FLOW_OK)
            {
                GST_ERROR_OBJECT(hailoenc, "Could not send empty frame %d", enc_params->picture_cnt);
                return ret;
            }
            break;
        }
        else
        {
            if (enc_params->multislice_encoding == 0)
            {
                ret = handle_frame_ready(hailoenc, frame, &planeFds, num_planes, is_dmabuf, false);
                if (ret != GST_FLOW_OK)
                {
                    GST_ERROR_OBJECT(hailoenc, "Could not send frame %d", enc_params->picture_cnt);
                    return ret;
                }
                // || (frame->system_frame_number % 2 == 0 && enc_params->nextCodingType == VCENC_INTRA_FRAME))
                if (hailoenc->params->update_config && enc_params->nextCodingType == VCENC_INTRA_FRAME)
                {
                    GST_INFO_OBJECT(hailoenc, "Finished GOP, restarting encoder in order to update config");
                    hailoenc->params->stream_restart = TRUE;
                    if (hailoenc->params->is_user_set_bitrate)
                    {
                        // Disable zoom boost feature
                        hailoenc->params->settings_boost_start_time_ns = 0;
                        apply_constant_optical_zoom_boost(hailoenc, current_optical_zoom);
                        hailoenc->params->is_user_set_bitrate = FALSE;
                    }
                }
            }
            UpdateEncoderGOP(enc_params);
        }
        break;
    default:
        GST_ERROR_OBJECT(hailoenc, "Encoder failed with error %d", enc_ret);
        ret = GST_FLOW_OK;
        if (is_dmabuf)
        {
            for (int32_t i = 0; i < num_planes; i++)
                releaseDmabuf(hailoenc, planeFds[i]);
        }
        return ret;
    }

    if (is_dmabuf)
    {
        GST_DEBUG_OBJECT(hailoenc, "Unsharing dmabuf");
        for (int32_t i = 0; i < num_planes; i++)
        {
            if (releaseDmabuf(hailoenc, planeFds[i]) != GST_FLOW_OK)
            {
                GST_ERROR_OBJECT(hailoenc, "Could not get physical address of plane %d", i);
            }
        }
    }
    return ret;
}

/**
 * Encode multiple frames - encode 1 frame or more according to the GOP config order.
 *
 * @param[in] encoder     The GstVideoEncoder object.
 * @return Upon success, returns GST_FLOW_OK, GST_FLOW_ERROR on failure.
 * @note All the frames that will be encoded are queued in the GstVideoEncoder object and retreived
 *       via the gst_video_encoder_get_frame function.
 */
static GstFlowReturn gst_hailoenc_encode_frames(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    GstVideoCodecFrame *current_frame;
    GstFlowReturn ret = GST_FLOW_ERROR;
    guint gop_size = enc_params->encIn.gopSize;
    GST_DEBUG_OBJECT(hailoenc, "Encoding %u frames", gop_size);

    if (hailoenc->params->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return GST_FLOW_ERROR;
    }

    if (gop_size <= 0)
    {
        GST_ERROR_OBJECT(hailoenc, "Invalid current GOP size %d", gop_size);
        return GST_FLOW_ERROR;
    }

    // Assuming enc_params->encIn.gopSize is not 0.
    for (guint i = 0; i < gop_size; i++)
    {
        current_frame = gst_video_encoder_get_frame(encoder, enc_params->picture_cnt);
        if (!current_frame)
        {
            GST_ERROR_OBJECT(hailoenc, "frame %u is missing", enc_params->picture_cnt);
            break;
        }
        ret = encode_single_frame(hailoenc, current_frame);
        gst_video_codec_frame_unref(current_frame);
        if (ret == GST_FLOW_FLUSHING)
        {
            GST_WARNING_OBJECT(hailoenc, "Pad is flushing, not sending more frames");
            break;
        }
        if (ret != GST_FLOW_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Encoding frame %u failed.", enc_params->picture_cnt);
            break;
        }
    }

    return ret;
}

/********************************
GstVideoEncoder Virtual Functions
********************************/

static gboolean gst_hailoenc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
    // GstCaps *other_caps;
    GstCaps *allowed_caps;
    GstCaps *icaps;
    GstVideoCodecState *output_format;
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    gboolean updated_caps = gst_hailoenc_update_params(hailoenc, &(state->info));
    if (hailoenc->params->encoder_instance != NULL && updated_caps)
    {
        GST_INFO_OBJECT(hailoenc, "Encoder parameters changed, restarting encoder");
        hailoenc->params->stream_restart = TRUE;
        hailoenc->params->hard_restart = TRUE;
    }
    else if (hailoenc->params->encoder_instance == NULL)
    {
        /* Encoder initialization */
        if (OpenEncoder(&(hailoenc->params->encoder_instance), enc_params) != 0)
        {
            return FALSE;
        }

        VCEncRet ret = gst_hailoenc_encode_header(encoder);
        if (ret != VCENC_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Failed to encode headers, returned %d", ret);
            return FALSE;
        }
    }
    /* some codecs support more than one format, first auto-choose one */
    GST_DEBUG_OBJECT(hailoenc, "picking an output format ...");
    allowed_caps = gst_pad_get_allowed_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder));
    if (!allowed_caps)
    {
        GST_DEBUG_OBJECT(hailoenc, "... but no peer, using template caps");
        /* we need to copy because get_allowed_caps returns a ref, and
         * get_pad_template_caps doesn't */
        allowed_caps = gst_pad_get_pad_template_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder));
    }
    GST_DEBUG_OBJECT(hailoenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

    icaps = gst_caps_fixate(allowed_caps);

    /* Store input state and set output state */
    if (hailoenc->params->input_state)
        gst_video_codec_state_unref(hailoenc->params->input_state);
    hailoenc->params->input_state = gst_video_codec_state_ref(state);
    GST_DEBUG_OBJECT(hailoenc, "Setting output caps state %" GST_PTR_FORMAT, icaps);

    output_format = gst_video_encoder_set_output_state(encoder, icaps, state);
    GST_DEBUG_OBJECT(hailoenc, "Encoder output width %d, height %d", GST_VIDEO_INFO_WIDTH(&(output_format->info)),
                     GST_VIDEO_INFO_HEIGHT(&(output_format->info)));

    gst_video_codec_state_unref(output_format);

    /* Store some tags */
    {
        GstTagList *tags = gst_tag_list_new_empty();
        gst_video_encoder_merge_tags(encoder, tags, GST_TAG_MERGE_REPLACE);
        gst_tag_list_unref(tags);
    }
    gint max_delayed_frames = 5;
    GstClockTime latency;
    latency = gst_util_uint64_scale_ceil(GST_SECOND * 1, max_delayed_frames, 25);
    gst_video_encoder_set_latency(GST_VIDEO_ENCODER(encoder), latency, latency);

    return TRUE;
}

static gboolean gst_hailoenc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    GST_DEBUG_OBJECT(hailoenc, "hailoenc propose allocation callback");

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

    return GST_VIDEO_ENCODER_CLASS(parent_class)->propose_allocation(encoder, query);
}

static gboolean gst_hailoenc_flush(GstVideoEncoder *)
{
    return TRUE;
}

static gboolean gst_hailoenc_start(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    VCEncIn *pEncIn = &(enc_params->encIn);

    if (VCEncInitGopConfigs(enc_params->gopSize, NULL, &(enc_params->encIn.gopConfig), enc_params->gopCfgOffset,
                            enc_params->bFrameQpDelta, enc_params->codecH264) != 0)
    {
        return FALSE;
    }

    /* Allocate input and output buffers */
    if (AllocRes(enc_params) != 0)
    {
        FreeRes(enc_params);
        return FALSE;
    }
    pEncIn->timeIncrement = 0;
    pEncIn->vui_timing_info_enable = 1;
    pEncIn->busOutBuf = enc_params->outbufMem.busAddress;
    pEncIn->outBufSize = enc_params->outbufMem.size;
    pEncIn->pOutBuf = enc_params->outbufMem.virtualAddress;

    gst_video_encoder_set_min_pts(encoder, GST_SECOND);

    return TRUE;
}

static gboolean gst_hailoenc_stop(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    if (hailoenc->params->encoder_instance != NULL)
    {
        CloseEncoder(hailoenc->params->encoder_instance);
        hailoenc->params->encoder_instance = NULL;
    }
    g_queue_clear(hailoenc->params->dts_queue);
    g_queue_free(hailoenc->params->dts_queue);
    FreeRes(enc_params);

    return TRUE;
}

static GstFlowReturn gst_hailoenc_finish(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    VCEncOut *pEncOut = &(hailoenc->params->enc_params.encOut);
    VCEncIn *pEncIn = &(hailoenc->params->enc_params.encIn);
    VCEncRet enc_ret;
    GstBuffer *eos_buf;

    /* End stream */
    enc_ret = VCEncStrmEnd(hailoenc->params->encoder_instance, pEncIn, pEncOut);
    if (enc_ret != VCENC_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Failed to end stream, returned %d", enc_ret);
        return GST_FLOW_ERROR;
    }
    eos_buf = gst_hailoenc_get_encoded_buffer(hailoenc);
    eos_buf->pts = eos_buf->dts = GPOINTER_TO_UINT(g_queue_peek_tail(hailoenc->params->dts_queue));
    return gst_pad_push(encoder->srcpad, eos_buf);
}

static void gst_hailoenc_handle_timestamps(GstHailoEnc *hailoenc, GstVideoCodecFrame *frame)
{
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    if (enc_params->picture_enc_cnt == 0)
    {
        if (hailoenc->params->adapt_framerate)
        {
            hailoenc->params->framerate_counter = 1;
            hailoenc->params->last_timestamp = timestamp;
        }
        switch (enc_params->gopSize)
        {
        case 1:
            break;
        case 2:
        case 3:
            g_queue_push_tail(hailoenc->params->dts_queue, GUINT_TO_POINTER(frame->pts - frame->duration));
            break;
        default:
            g_queue_push_tail(hailoenc->params->dts_queue, GUINT_TO_POINTER(frame->pts - 2 * frame->duration));
            g_queue_push_tail(hailoenc->params->dts_queue, GUINT_TO_POINTER(frame->pts - frame->duration));
            break;
        }
    }
    else if (hailoenc->params->adapt_framerate)
    {
        hailoenc->params->framerate_counter++;
        auto timediff_ms = media_library_difftimespec_ms(timestamp, hailoenc->params->last_timestamp);
        if (timediff_ms > 1000 || hailoenc->params->framerate_counter == 10)
        {
            float avg_duration_s = (float)timediff_ms / (float)hailoenc->params->framerate_counter / 1000.0f;
            float new_framerate = 1.0f / avg_duration_s;
            float current_framerate = (float)enc_params->frameRateNumer / (float)enc_params->frameRateDenom;
            if (std::max(new_framerate, current_framerate) / std::min(new_framerate, current_framerate) >=
                hailoenc->params->framerate_tolerance)
            {
                GST_WARNING_OBJECT(hailoenc, "Framerate changed from %d to %d", (int)current_framerate,
                                   (int)std::round(new_framerate));
                enc_params->frameRateNumer = (u32)std::round(new_framerate);
                enc_params->frameRateDenom = 1;
                hailoenc->params->update_config = TRUE;
                hailoenc->params->hard_restart = TRUE;
            }
            hailoenc->params->framerate_counter = 0;
            hailoenc->params->last_timestamp = timestamp;
        }
    }
    g_queue_push_tail(hailoenc->params->dts_queue, GUINT_TO_POINTER(frame->pts));
}

static GstFlowReturn gst_hailoenc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    GstFlowReturn ret = GST_FLOW_ERROR;
    EncoderParams *enc_params = &(hailoenc->params->enc_params);
    GList *frames;
    guint delayed_frames;
    GstVideoCodecFrame *oldest_frame;
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);
    GST_DEBUG_OBJECT(hailoenc, "Received frame number %u", frame->system_frame_number);

    if (enc_params->picture_enc_cnt == 0)
    {
        // Set high-priority to encoder thread in order to achieve expected performance.
        // This will change the priority of exactly one thread for each encoder instance.
        int nice_value = -20;
        setpriority(PRIO_PROCESS, gettid(), nice_value);
        GST_DEBUG_OBJECT(hailoenc, "Set high-priority to encoder thread. nice value %d", nice_value);
    }

    gst_hailoenc_handle_timestamps(hailoenc, frame);

    if (hailoenc->params->stream_restart)
    {
        ret = gst_hailoenc_stream_restart(encoder);
        if (ret != GST_FLOW_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Failed to restart encoder");
            return ret;
        }
    }

    // Update Slice Encoding parameters
    enc_params->multislice_encoding = 0;
    enc_params->strmPtr = NULL;

    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(frame))
    {
        GST_DEBUG_OBJECT(hailoenc, "Forcing keyframe");
        // Adding sync point in order to delete forced keyframe evnet from the queue.
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
        ForceKeyframe(enc_params);
        oldest_frame = gst_video_encoder_get_oldest_frame(encoder);
        ret = encode_single_frame(hailoenc, oldest_frame);
        if (ret != GST_FLOW_OK)
        {
            GST_ERROR_OBJECT(hailoenc, "Failed to encode forced keyframe");
            return ret;
        }
        if (frame == oldest_frame)
        {
            gst_video_codec_frame_unref(oldest_frame);
            return ret;
        }
    }

    switch (enc_params->nextCodingType)
    {
    case VCENC_INTRA_FRAME: {
        ret = encode_single_frame(hailoenc, frame);
        break;
    }
    case VCENC_PREDICTED_FRAME: {
        frames = gst_video_encoder_get_frames(encoder);
        delayed_frames = g_list_length(frames);
        g_list_free_full(frames, (GDestroyNotify)gst_video_codec_frame_unref);
        guint gop_size = enc_params->encIn.gopSize;
        if (delayed_frames == gop_size)
        {
            ret = gst_hailoenc_encode_frames(encoder);
        }
        else if (delayed_frames < gop_size)
        {
            ret = GST_FLOW_OK;
        }
        else
        {
            GST_ERROR_OBJECT(hailoenc, "Skipped too many frames");
        }
        break;
    }
    case VCENC_BIDIR_PREDICTED_FRAME:
        GST_ERROR_OBJECT(hailoenc, "Got B frame without pending P frame");
        break;
    default:
        GST_ERROR_OBJECT(hailoenc, "Unknown coding type %d", (int)enc_params->nextCodingType);
        break;
    }
    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    GST_DEBUG_OBJECT(hailoenc, "handle_frame took %lu milliseconds",
                     (long)media_library_difftimespec_ms(end_handle, start_handle));
    return ret;
}
