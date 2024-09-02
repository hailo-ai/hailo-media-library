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
};

#define MIN_FRAMERATE_TOLERANCE (0)
#define MAX_FRAMERATE_TOLERANCE (500)
#define DEFAULT_FRAMERATE_TOLERANCE (15)

#define GST_TYPE_HAILOENC_COMPRESSOR (gst_hailoenc_compressor_get_type())
static GType
gst_hailoenc_compressor_get_type(void)
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
        hailoenc_compressor_type =
            g_enum_register_static("GstHailoEncCompressor", hailoenc_compressors);
    }
    return hailoenc_compressor_type;
}

#define GST_TYPE_HAILOENC_BLOCK_RC_SIZE (gst_hailoenc_block_rc_size_get_type())
static GType
gst_hailoenc_block_rc_size_get_type(void)
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
        hailoenc_block_rc_size_type =
            g_enum_register_static("GstHailoEncBlockRcSize", hailoenc_block_rc_size_types);
    }
    return hailoenc_block_rc_size_type;
}

/*******************
Function Definitions
*******************/

static void gst_hailoenc_class_init(GstHailoEncClass *klass);
static void gst_hailoenc_init(GstHailoEnc *hailoenc);
static void gst_hailoenc_set_property(GObject *object,
                                      guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoenc_get_property(GObject *object,
                                      guint prop_id, GValue *value, GParamSpec *pspec);
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
#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_hailoenc_debug, "hailoenc", 0, "hailoenc element");
#define gst_hailoenc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoEnc, gst_hailoenc, GST_TYPE_VIDEO_ENCODER, _do_init);

static void
gst_hailoenc_class_init(GstHailoEncClass *klass)
{
    GObjectClass *gobject_class;
    GstVideoEncoderClass *venc_class;

    gobject_class = (GObjectClass *)klass;
    venc_class = (GstVideoEncoderClass *)klass;

    gobject_class->set_property = gst_hailoenc_set_property;
    gobject_class->get_property = gst_hailoenc_get_property;

    g_object_class_install_property(gobject_class, PROP_INTRA_PIC_RATE,
                                    g_param_spec_uint("intra-pic-rate", "IDR Interval", "I frames interval (0 - Dynamic IDR Interval)",
                                                      MIN_INTRA_PIC_RATE, MAX_INTRA_PIC_RATE, (guint)DEFAULT_INTRA_PIC_RATE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_GOP_SIZE,
                                    g_param_spec_uint("gop-size", "GOP Size", "GOP Size (1 - No B Frames)",
                                                      MIN_GOP_SIZE, MAX_GOP_SIZE, (guint)DEFAULT_GOP_SIZE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_GOP_LENGTH,
                                    g_param_spec_uint("gop-length", "GOP Length", "GOP Length",
                                                      MIN_GOP_LENGTH, MAX_GOP_LENGTH, (guint)DEFAULT_GOP_LENGTH,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_QPHDR,
                                    g_param_spec_int("qp-hdr", "Initial target QP", "Initial target QP, -1 = Encoder calculates initial QP",
                                                     MIN_QPHDR, MAX_QPHDR, (gint)DEFAULT_QPHDR,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_QPMIN,
                                    g_param_spec_uint("qp-min", "QP Min", "Minimum frame header QP",
                                                      MIN_QP_VALUE, MAX_QP_VALUE, (guint)DEFAULT_QPMIN,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_QPMAX,
                                    g_param_spec_uint("qp-max", "QP Max", "Maximum frame header QP",
                                                      MIN_QP_VALUE, MAX_QP_VALUE, (guint)DEFAULT_QPMAX,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_INTRA_QP_DELTA,
                                    g_param_spec_int("intra-qp-delta", "Intra QP delta", "QP difference between target QP and intra frame QP",
                                                     MIN_INTRA_QP_DELTA, MAX_INTRA_QP_DELTA, (gint)DEFAULT_INTRA_QP_DELTA,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_FIXED_INTRA_QP,
                                    g_param_spec_uint("fixed-intra-qp", "Fixed Intra QP", "Use fixed QP value for every intra frame in stream, 0 = disabled",
                                                      MIN_FIXED_INTRA_QP, MAX_FIXED_INTRA_QP, (guint)DEFAULT_FIXED_INTRA_QP,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_BFRAME_QP_DELTA,
                                    g_param_spec_int("bframe-qp-delta", "BFrame QP Delta", "QP difference between BFrame QP and target QP, -1 = Disabled",
                                                     MIN_BFRAME_QP_DELTA, MAX_BFRAME_QP_DELTA, (gint)DEFAULT_BFRAME_QP_DELTA,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_BITRATE,
                                    g_param_spec_uint("bitrate", "Target bitrate", "Target bitrate for rate control in bits/second",
                                                      MIN_BITRATE, MAX_BITRATE, (guint)DEFAULT_BITRATE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_TOL_MOVING_BITRATE,
                                    g_param_spec_uint("tol-moving-bitrate", "Tolerance moving bitrate", "Percent tolerance over target bitrate of moving bit rate",
                                                      MIN_BITRATE_VARIABLE_RANGE, MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_TOL_MOVING_BITRATE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_BITRATE_VAR_RANGE_I,
                                    g_param_spec_uint("bitvar-range-i", "Bitrate percent variation I frame", "Percent variations over average bits per frame for I frame",
                                                      MIN_BITRATE_VARIABLE_RANGE, MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_BITVAR_RANGE_I,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_BITRATE_VAR_RANGE_P,
                                    g_param_spec_uint("bitvar-range-p", "Bitrate percent variation P frame", "Percent variations over average bits per frame for P frame",
                                                      MIN_BITRATE_VARIABLE_RANGE, MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_BITVAR_RANGE_P,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_BITRATE_VAR_RANGE_B,
                                    g_param_spec_uint("bitvar-range-b", "Bitrate percent variation B frame", "Percent variations over average bits per frame for B frame",
                                                      MIN_BITRATE_VARIABLE_RANGE, MAX_BITRATE_VARIABLE_RANGE, (guint)DEFAULT_BITVAR_RANGE_B,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_PICTURE_RC,
                                    g_param_spec_boolean("picture-rc", "Picture Rate Control", "Adjust QP between pictures", TRUE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_CTB_RC,
                                    g_param_spec_boolean("ctb-rc", "Block Rate Control", "Adaptive adjustment of QP inside frame", FALSE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_PICTURE_SKIP,
                                    g_param_spec_boolean("picture-skip", "Picture Skip", "Allow rate control to skip pictures", FALSE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_HRD,
                                    g_param_spec_boolean("hrd", "Picture Rate Control", "Restricts the instantaneous bitrate and total bit amount of every coded picture.", false,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_CVBR,
                                    g_param_spec_uint("cvbr", "Picture Rate Control", "Rate control mode, makes VBR more like CBR.",
                                                      MIN_CVBR_MODE, MAX_CVBR_MODE, (guint)DEFAULT_CVBR_MODE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_PADDING,
                                    g_param_spec_boolean("padding", "Picture Rate Control", "Add padding to buffers on RC underflow.", false,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_MONITOR_FRAMES,
                                    g_param_spec_uint("monitor-frames", "Monitor Frames", "How many frames will be monitored for moving bit rate. Default is using framerate",
                                                      MIN_MONITOR_FRAMES, MAX_MONITOR_FRAMES, (gint)DEFAULT_MONITOR_FRAMES,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_ROI_AREA_1,
                                    g_param_spec_string("roi-area1", "ROI Area and QP Delta",
                                                        "Specifying rectangular area of CTBs as Region Of Interest with lower QP, left:top:right:bottom:delta_qp format ", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_ROI_AREA_2,
                                    g_param_spec_string("roi-area2", "ROI Area and QP Delta",
                                                        "Specifying rectangular area of CTBs as Region Of Interest with lower QP, left:top:right:bottom:delta_qp format ", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_COMPRESSOR,
                                    g_param_spec_enum("compressor", "Compressor", "Enable/Disable Embedded Compression",
                                                      GST_TYPE_HAILOENC_COMPRESSOR, (guint)3,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_BLOCK_RC_SIZE,
                                    g_param_spec_enum("block-rc-size", "Block Rate Control Size", "Size of block rate control",
                                                      GST_TYPE_HAILOENC_BLOCK_RC_SIZE, (guint)0,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_HRD_CPB_SIZE,
                                    g_param_spec_uint("hrd-cpb-size", "HRD Coded Picture Buffer size", "Buffer size used by the HRD model in bits",
                                                      MIN_HRD_CPB_SIZE, MAX_HRD_CPB_SIZE, (guint)DEFAULT_HRD_CPB_SIZE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_ADAPT_FRAMERATE,
                                    g_param_spec_boolean("adapt-framerate", "Adapt Framerate", "Adapt encoder to real framerate", FALSE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_FRAMERATE_TOLERANCE,
                                    g_param_spec_uint("framerate-tolerance", "Framerate Tolerance", "Framerate tolerance in percent. Relevant only if adapt-framerate is enabled",
                                                      MIN_FRAMERATE_TOLERANCE, MAX_FRAMERATE_TOLERANCE, (guint)DEFAULT_FRAMERATE_TOLERANCE,
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

static void
gst_hailoenc_init(GstHailoEnc *hailoenc)
{
    EncoderParams *enc_params = &(hailoenc->enc_params);
    hailoenc->apiVer = VCEncGetApiVersion();
    hailoenc->encBuild = VCEncGetBuild();
    hailoenc->stream_restart = FALSE;
    memset(enc_params, 0, sizeof(EncoderParams));
    memset(hailoenc->gopPicCfg, 0, sizeof(hailoenc->gopPicCfg));
    hailoenc->encoder_instance = NULL;
    enc_params->encIn.gopConfig.pGopPicCfg = hailoenc->gopPicCfg;
    hailoenc->adapt_framerate = FALSE;
    hailoenc->framerate_tolerance = 1.15f;
    hailoenc->dts_queue = g_queue_new();
    g_queue_init(hailoenc->dts_queue);
    int sched_policy = 0;
    sched_param sch_params;
    memset(&sch_params, 0, sizeof(sch_params));
    pthread_getschedparam(pthread_self(), &sched_policy, &sch_params);
    sch_params.sched_priority -= 10;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sch_params);
}

/************************
GObject Virtual Functions
************************/

static void
gst_hailoenc_get_property(GObject *object,
                          guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)(object);

    switch (prop_id)
    {
    case PROP_INTRA_PIC_RATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.intraPicRate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.gopSize);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_LENGTH:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.gopLength);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPHDR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, (gint)hailoenc->enc_params.qphdr);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMIN:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.qpmin);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMAX:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.qpmax);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_INTRA_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, (gint)hailoenc->enc_params.intra_qp_delta);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FIXED_INTRA_QP:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.fixed_intra_qp);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BFRAME_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_int(value, (gint)hailoenc->enc_params.bFrameQpDelta);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.bitrate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_TOL_MOVING_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.tolMovingBitRate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_I:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.bitVarRangeI);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_P:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.bitVarRangeP);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_B:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.bitVarRangeB);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_MONITOR_FRAMES:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.monitorFrames);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PICTURE_RC:
    {
        GST_OBJECT_LOCK(hailoenc);
        gboolean picture_rc = hailoenc->enc_params.pictureRc == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, picture_rc);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_CTB_RC:
    {
        GST_OBJECT_LOCK(hailoenc);
        gboolean ctb_rc = hailoenc->enc_params.ctbRc == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, ctb_rc);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_PICTURE_SKIP:
    {
        GST_OBJECT_LOCK(hailoenc);
        gboolean picture_skip = hailoenc->enc_params.pictureSkip == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, picture_skip);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_HRD:
    {
        GST_OBJECT_LOCK(hailoenc);
        gboolean hrd = hailoenc->enc_params.hrd == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, hrd);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_CVBR:
    {
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.cvbr);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_PADDING:
    {
        GST_OBJECT_LOCK(hailoenc);
        gboolean padding = hailoenc->enc_params.padding == 1 ? TRUE : FALSE;
        g_value_set_boolean(value, padding);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    }
    case PROP_ROI_AREA_1:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_string(value, (const gchar *)hailoenc->enc_params.roiArea1);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ROI_AREA_2:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_string(value, (const gchar *)hailoenc->enc_params.roiArea2);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_COMPRESSOR:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_enum(value, (gint)hailoenc->enc_params.compressor);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BLOCK_RC_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_enum(value, (gint)hailoenc->enc_params.blockRcSize);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_HRD_CPB_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)hailoenc->enc_params.hrdCpbSize);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ADAPT_FRAMERATE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_boolean(value, hailoenc->adapt_framerate);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FRAMERATE_TOLERANCE:
        GST_OBJECT_LOCK(hailoenc);
        g_value_set_uint(value, (guint)((hailoenc->framerate_tolerance - 1.0f) * 100.0f));
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_hailoenc_set_property(GObject *object,
                          guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)(object);
    hailoenc->update_config = hailoenc->enc_params.picture_enc_cnt != 0;

    switch (prop_id)
    {
    case PROP_INTRA_PIC_RATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.intraPicRate = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.gopSize = g_value_get_uint(value);
        if (hailoenc->enc_params.gopSize > MAX_GOP_SIZE)
        {
            GST_WARNING_OBJECT(hailoenc, "GOP size %d is too large, setting to max %d", hailoenc->enc_params.gopSize, MAX_GOP_SIZE);
            hailoenc->enc_params.gopSize = MAX_GOP_SIZE;
        }
        else if (hailoenc->enc_params.gopSize < MIN_GOP_SIZE)
        {
            GST_WARNING_OBJECT(hailoenc, "GOP size %d is too small, setting to min %d", hailoenc->enc_params.gopSize, MIN_GOP_SIZE);
            hailoenc->enc_params.gopSize = MIN_GOP_SIZE;
        }
        hailoenc->update_gop_size = TRUE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_GOP_LENGTH:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.gopLength = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPHDR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.qphdr = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMIN:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.qpmin = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_QPMAX:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.qpmax = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_INTRA_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.intra_qp_delta = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FIXED_INTRA_QP:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.fixed_intra_qp = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BFRAME_QP_DELTA:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.bFrameQpDelta = g_value_get_int(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.bitrate = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_TOL_MOVING_BITRATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.tolMovingBitRate = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_I:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.bitVarRangeI = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_P:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.bitVarRangeP = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BITRATE_VAR_RANGE_B:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.bitVarRangeB = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_MONITOR_FRAMES:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.monitorFrames = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PICTURE_RC:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.pictureRc = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CTB_RC:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.ctbRc = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PICTURE_SKIP:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.pictureSkip = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_HRD:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.hrd = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_CVBR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.cvbr = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_PADDING:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.padding = g_value_get_boolean(value) ? 1 : 0;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ROI_AREA_1:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.roiArea1 = (char *)g_value_get_string(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ROI_AREA_2:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.roiArea2 = (char *)g_value_get_string(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_COMPRESSOR:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.compressor = (u32)g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_BLOCK_RC_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.blockRcSize = (u32)g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_HRD_CPB_SIZE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->enc_params.hrdCpbSize = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_ADAPT_FRAMERATE:
        GST_OBJECT_LOCK(hailoenc);
        hailoenc->adapt_framerate = g_value_get_boolean(value);
        hailoenc->update_config = FALSE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    case PROP_FRAMERATE_TOLERANCE:
        GST_OBJECT_LOCK(hailoenc);
        GST_WARNING_OBJECT(hailoenc, "Setting framerate tolerance to %d", g_value_get_uint(value));
        hailoenc->framerate_tolerance = (float)((float)g_value_get_uint(value) / 100.0f + 1.0f);
        hailoenc->update_config = FALSE;
        GST_OBJECT_UNLOCK(hailoenc);
        break;
    default:
        hailoenc->update_config = FALSE;
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_hailoenc_finalize(GObject *object)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)object;
    GST_DEBUG_OBJECT(hailoenc, "hailoenc finalize callback");

    /* clean up remaining allocated data */

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_hailoenc_dispose(GObject *object)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)object;
    GST_DEBUG_OBJECT(hailoenc, "hailoenc dispose callback");

    /* clean up as possible.  may be called multiple times */
    if (hailoenc->encoder_instance)
    {
        CloseEncoder(hailoenc->encoder_instance);
        hailoenc->encoder_instance = NULL;
        FreeRes(&(hailoenc->enc_params));
        if (hailoenc->dts_queue)
        {
            g_queue_clear(hailoenc->dts_queue);
            g_queue_free(hailoenc->dts_queue);
            hailoenc->dts_queue = NULL;
        }
    }
    if (hailoenc->input_state)
    {
        gst_video_codec_state_unref(hailoenc->input_state);
        hailoenc->input_state = NULL;
    }
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*****************
Internal Functions
*****************/

/**
 * Updates the encoder with the input video info.
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] info         A GstVideoInfo object containing the input video info for this pipeline.
 * @returns TRUE if the encoder parameters were updated, FALSE otherwise.
 * @note The updated data is the resolution, framerate and input format.
 */
static gboolean
gst_hailoenc_update_params(GstHailoEnc *hailoenc, GstVideoInfo *info)
{
    gboolean updated_params = FALSE;
    EncoderParams *enc_params = &(hailoenc->enc_params);

    if (enc_params->width != GST_VIDEO_INFO_WIDTH(info) ||
        enc_params->height != GST_VIDEO_INFO_HEIGHT(info))
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
static GstFlowReturn gst_hailoenc_update_input_buffer(GstHailoEnc *hailoenc,
                                                      HailoMediaLibraryBufferPtr hailo_buffer)
{
    EncoderParams *enc_params = &(hailoenc->enc_params);
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
        GST_ERROR_OBJECT(hailoenc, "luma %p luma_size %zu chroma %p chroma_size %zu", luma, luma_size, chroma, chroma_size);
        return GST_FLOW_ERROR;
    }

    if (stride != enc_params->stride)
    {
        GST_WARNING_OBJECT(hailoenc, "Stride changed from %u to %u", enc_params->stride, stride);
        enc_params->stride = stride;
        InitEncoderPreProcConfig(enc_params, &(hailoenc->encoder_instance));
    }

    if (hailo_buffer->is_dmabuf())
    {
        lumaFd = hailo_buffer->get_fd(0);
        chromaFd = hailo_buffer->get_fd(1);
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
        luma = static_cast<uint32_t *>(hailo_buffer->get_plane(0));
        chroma = static_cast<uint32_t *>(hailo_buffer->get_plane(1));
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
    EncoderParams *enc_params = &(hailoenc->enc_params);
    outbuf = gst_buffer_new_memdup(enc_params->outbufMem.virtualAddress,
                                   enc_params->encOut.streamSize);
    return outbuf;
}

/**
 * Add headers to the encoded stream
 *
 * @param[in] hailoenc       The HailoEnc gstreamer instance.
 * @param[in] new_header     The new header as GstBuf fer object.
 */
static void
gst_hailoenc_add_headers(GstHailoEnc *hailoenc, GstBuffer *new_header)
{
    if (hailoenc->header_buffer)
        hailoenc->header_buffer = gst_buffer_append(hailoenc->header_buffer, new_header);
    else
        hailoenc->header_buffer = new_header;
}

/**
 * Encode and set the header - Performed via VCEncStrmStart
 *
 * @param[in] encoder     The GstVideoEncoder object.
 * @return Upon success, returns VCENC_OK. Otherwise, returns another error value from VCEncRet.
 */
static VCEncRet
gst_hailoenc_encode_header(GstVideoEncoder *encoder)
{
    VCEncRet enc_ret;
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->enc_params);
    VCEncIn *pEncIn = &(enc_params->encIn);
    VCEncOut *pEncOut = &(enc_params->encOut);
    pEncIn->gopSize = enc_params->gopSize;

    if (hailoenc->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return VCENC_ERROR;
    }
    enc_ret = VCEncStrmStart(hailoenc->encoder_instance, pEncIn, pEncOut);
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
static GstFlowReturn
gst_hailoenc_stream_restart(GstVideoEncoder *encoder, GstVideoCodecFrame *frame)
{
    VCEncRet enc_ret;
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->enc_params);
    VCEncIn *pEncIn = &(enc_params->encIn);
    VCEncOut *pEncOut = &(enc_params->encOut);
    GST_WARNING_OBJECT(hailoenc, "Restarting encoder");

    if (hailoenc->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return GST_FLOW_ERROR;
    }

    enc_ret = VCEncStrmEnd(hailoenc->encoder_instance, pEncIn, pEncOut);
    if (enc_ret != VCENC_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to end stream, returned %d", enc_ret);
        return GST_FLOW_ERROR;
    }

    if (enc_params->picture_enc_cnt == 0)
    {
        gst_buffer_unref(hailoenc->header_buffer);
        hailoenc->header_buffer = NULL;
    }

    if (hailoenc->hard_restart)
    {
        CloseEncoder(hailoenc->encoder_instance);
    }

    if (hailoenc->update_gop_size)
    {
        GST_DEBUG_OBJECT(hailoenc, "Updating gop size to %u", enc_params->gopSize);
        memset(hailoenc->gopPicCfg, 0, sizeof(hailoenc->gopPicCfg));
        memset(enc_params->gopCfgOffset, 0, sizeof(enc_params->gopCfgOffset));
        memset(&(enc_params->encIn.gopConfig), 0, sizeof(enc_params->encIn.gopConfig));
        enc_params->encIn.gopConfig.pGopPicCfg = hailoenc->gopPicCfg;
        if (VCEncInitGopConfigs(enc_params->gopSize, NULL, &(enc_params->encIn.gopConfig), enc_params->gopCfgOffset, enc_params->bFrameQpDelta, enc_params->codecH264) != 0)
        {
            GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to update gop size");
            return GST_FLOW_ERROR;
        }
        hailoenc->update_gop_size = FALSE;
    }

    if (hailoenc->hard_restart)
    {
        GST_INFO_OBJECT(hailoenc, "Reopening encoder");
        if (OpenEncoder(&(hailoenc->encoder_instance), enc_params) != 0)
        {
            GST_ERROR_OBJECT(hailoenc, "Encoder restart - Failed to reopen encoder");
            return GST_FLOW_ERROR;
        }
        hailoenc->hard_restart = FALSE;
    }
    else
    {
        if (UpdateEncoderConfig(&(hailoenc->encoder_instance), enc_params) != 0)
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

    hailoenc->update_config = FALSE;
    hailoenc->stream_restart = FALSE;
    return GST_FLOW_OK;
}

/*
 * Send a slice to the downstream element
 *
 * @param[in] hailoenc     The GstHailoEnc object.
 * @param[in] address      The address of the slice.
 * @param[in] size         The size of the slice.
 * @return Upon success, returns GST_FLOW_OK, GST_FLOW_ERROR on failure.
 */
static GstFlowReturn
gst_hailoenc_send_slice(GstHailoEnc *hailoenc, u32 *address, u32 size)
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
    EncoderParams *enc_params = &(hailoenc->enc_params);
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
            for (i = (slice->nalUnitInfoNum + slice->slicesReadyPrev); i < slice->slicesReady + slice->nalUnitInfoNum; i++)
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
    if (EWLUnshareDmabuf(hailoenc->enc_params.ewl, fd) != EWL_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Could not unshare dmabuf");
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

static GstFlowReturn handle_frame_ready(GstHailoEnc *hailoenc, GstVideoCodecFrame *frame,
                                        std::vector<int> *planeFds, int num_planes, bool is_dmabuf, bool send_null_buffer)
{
    EncoderParams *enc_params = &(hailoenc->enc_params);
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

    frame->dts = GPOINTER_TO_UINT(g_queue_pop_head(hailoenc->dts_queue));
    frame->duration = GST_SECOND / enc_params->frameRateNumer * enc_params->frameRateDenom;

    if (send_null_buffer)
        frame->output_buffer = null_buffer;
    else
        frame->output_buffer = gst_hailoenc_get_encoded_buffer(hailoenc);

    if (hailoenc->header_buffer)
    {
        frame->output_buffer = gst_buffer_append(hailoenc->header_buffer, frame->output_buffer);
        hailoenc->header_buffer = NULL;
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
    EncoderParams *enc_params = &(hailoenc->enc_params);
    struct timespec start_encode, end_encode;
    GST_DEBUG_OBJECT(hailoenc, "Encoding frame number %u in type %u", frame->system_frame_number, enc_params->nextCodingType);

    if (hailoenc->encoder_instance == NULL)
    {
        GST_ERROR_OBJECT(hailoenc, "Encoder not initialized");
        return GST_FLOW_ERROR;
    }
    hailo_buffer = hailo_buffer_from_gst_buffer(frame->input_buffer, hailoenc->input_state->caps, true);
    if (!hailo_buffer)
    {
        GST_ERROR_OBJECT(hailoenc, "Could not get hailo buffer");
        return GST_FLOW_ERROR;
    }

    if (hailo_buffer->is_dmabuf())
    {
        is_dmabuf = true;
        num_planes = hailo_buffer->get_num_of_planes();
        planeFds.resize(num_planes);
        for (int32_t i = 0; i < num_planes; i++)
        {
            planeFds[i] = hailo_buffer->get_fd(i);
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
    enc_ret = EncodeFrame(enc_params, hailoenc->encoder_instance, &gst_hailoenc_slice_ready, hailoenc);
    clock_gettime(CLOCK_MONOTONIC, &end_encode);
    GST_DEBUG_OBJECT(hailoenc, "Encode took %lu milliseconds", (long)media_library_difftimespec_ms(end_encode, start_encode));
    GST_DEBUG_OBJECT(hailoenc, "Encode performance is %d cycles", VCEncGetPerformance(hailoenc->encoder_instance));

    switch (enc_ret)
    {
    case VCENC_FRAME_READY:
        enc_params->picture_enc_cnt++;
        if (enc_params->encOut.streamSize == 0)
        {
            ret = GST_FLOW_OK;
            GST_WARNING_OBJECT(hailoenc, "Encoder didn't return any output for frame %d", enc_params->picture_cnt);

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
            }
            UpdateEncoderGOP(enc_params, hailoenc->encoder_instance);
        }
        break;
    default:
        GST_ERROR_OBJECT(hailoenc, "Encoder failed with error %d", enc_ret);
        ret = GST_FLOW_ERROR;
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
static GstFlowReturn
gst_hailoenc_encode_frames(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->enc_params);
    GstVideoCodecFrame *current_frame;
    GstFlowReturn ret = GST_FLOW_ERROR;
    guint gop_size = enc_params->encIn.gopSize;
    GST_DEBUG_OBJECT(hailoenc, "Encoding %u frames", gop_size);

    if (hailoenc->encoder_instance == NULL)
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

    if (hailoenc->update_config && enc_params->nextCodingType == VCENC_INTRA_FRAME)
    {
        GST_INFO_OBJECT(hailoenc, "Finished GOP, restarting encoder in order to update config");
        hailoenc->stream_restart = TRUE;
    }

    return ret;
}

/********************************
GstVideoEncoder Virtual Functions
********************************/

static gboolean
gst_hailoenc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
    // GstCaps *other_caps;
    GstCaps *allowed_caps;
    GstCaps *icaps;
    GstVideoCodecState *output_format;
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->enc_params);

    gboolean updated_caps = gst_hailoenc_update_params(hailoenc, &(state->info));
    if (hailoenc->encoder_instance != NULL && updated_caps)
    {
        GST_INFO_OBJECT(hailoenc, "Encoder parameters changed, restarting encoder");
        hailoenc->stream_restart = TRUE;
        hailoenc->hard_restart = TRUE;
    }
    else if (hailoenc->encoder_instance == NULL)
    {
        /* Encoder initialization */
        if (OpenEncoder(&(hailoenc->encoder_instance), enc_params) != 0)
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
        allowed_caps =
            gst_pad_get_pad_template_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder));
    }
    GST_DEBUG_OBJECT(hailoenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

    icaps = gst_caps_fixate(allowed_caps);

    /* Store input state and set output state */
    if (hailoenc->input_state)
        gst_video_codec_state_unref(hailoenc->input_state);
    hailoenc->input_state = gst_video_codec_state_ref(state);
    GST_DEBUG_OBJECT(hailoenc, "Setting output caps state %" GST_PTR_FORMAT, icaps);

    output_format = gst_video_encoder_set_output_state(encoder, icaps, state);
    GST_DEBUG_OBJECT(hailoenc, "Encoder output width %d, height %d", GST_VIDEO_INFO_WIDTH(&(output_format->info)), GST_VIDEO_INFO_HEIGHT(&(output_format->info)));

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

static gboolean
gst_hailoenc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    GST_DEBUG_OBJECT(hailoenc, "hailoenc propose allocation callback");

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

    return GST_VIDEO_ENCODER_CLASS(parent_class)->propose_allocation(encoder, query);
}

static gboolean
gst_hailoenc_flush(GstVideoEncoder *encoder)
{
    return TRUE;
}

static gboolean
gst_hailoenc_start(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->enc_params);
    VCEncIn *pEncIn = &(enc_params->encIn);

    if (VCEncInitGopConfigs(enc_params->gopSize, NULL, &(enc_params->encIn.gopConfig), enc_params->gopCfgOffset, enc_params->bFrameQpDelta, enc_params->codecH264) != 0)
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
    pEncIn->busOutBuf = enc_params->outbufMem.busAddress;
    pEncIn->outBufSize = enc_params->outbufMem.size;
    pEncIn->pOutBuf = enc_params->outbufMem.virtualAddress;

    gst_video_encoder_set_min_pts(encoder, GST_SECOND);

    return TRUE;
}

static gboolean
gst_hailoenc_stop(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    EncoderParams *enc_params = &(hailoenc->enc_params);
    if (hailoenc->encoder_instance != NULL)
    {
        CloseEncoder(hailoenc->encoder_instance);
        hailoenc->encoder_instance = NULL;
    }
    g_queue_clear(hailoenc->dts_queue);
    g_queue_free(hailoenc->dts_queue);
    FreeRes(enc_params);

    return TRUE;
}

static GstFlowReturn
gst_hailoenc_finish(GstVideoEncoder *encoder)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    VCEncOut *pEncOut = &(hailoenc->enc_params.encOut);
    VCEncIn *pEncIn = &(hailoenc->enc_params.encIn);
    VCEncRet enc_ret;
    GstBuffer *eos_buf;

    /* End stream */
    enc_ret = VCEncStrmEnd(hailoenc->encoder_instance, pEncIn, pEncOut);
    if (enc_ret != VCENC_OK)
    {
        GST_ERROR_OBJECT(hailoenc, "Failed to end stream, returned %d", enc_ret);
        return GST_FLOW_ERROR;
    }
    eos_buf = gst_hailoenc_get_encoded_buffer(hailoenc);
    eos_buf->pts = eos_buf->dts = GPOINTER_TO_UINT(g_queue_peek_tail(hailoenc->dts_queue));
    return gst_pad_push(encoder->srcpad, eos_buf);
}

static void gst_hailoenc_handle_timestamps(GstHailoEnc *hailoenc, GstVideoCodecFrame *frame)
{
    EncoderParams *enc_params = &(hailoenc->enc_params);
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    if (enc_params->picture_enc_cnt == 0)
    {
        if (hailoenc->adapt_framerate)
        {
            hailoenc->framerate_counter = 1;
            hailoenc->last_timestamp = timestamp;
        }
        switch (enc_params->gopSize)
        {
        case 1:
            break;
        case 2:
        case 3:
            g_queue_push_tail(hailoenc->dts_queue, GUINT_TO_POINTER(frame->pts - frame->duration));
            break;
        default:
            g_queue_push_tail(hailoenc->dts_queue, GUINT_TO_POINTER(frame->pts - 2 * frame->duration));
            g_queue_push_tail(hailoenc->dts_queue, GUINT_TO_POINTER(frame->pts - frame->duration));
            break;
        }
    }
    else if (hailoenc->adapt_framerate)
    {
        hailoenc->framerate_counter++;
        auto timediff_ms = media_library_difftimespec_ms(timestamp, hailoenc->last_timestamp);
        if (timediff_ms > 1000 || hailoenc->framerate_counter == 10)
        {
            float avg_duration_s = (float)timediff_ms / (float)hailoenc->framerate_counter / 1000.0f;
            float new_framerate = 1.0f / avg_duration_s;
            float current_framerate = (float)enc_params->frameRateNumer / (float)enc_params->frameRateDenom;
            if (std::max(new_framerate, current_framerate) / std::min(new_framerate, current_framerate) >= hailoenc->framerate_tolerance)
            {
                GST_WARNING_OBJECT(hailoenc, "Framerate changed from %d to %d", (int)current_framerate, (int)std::round(new_framerate));
                enc_params->frameRateNumer = (u32)std::round(new_framerate);
                enc_params->frameRateDenom = 1;
                hailoenc->update_config = TRUE;
                hailoenc->hard_restart = TRUE;
            }
            hailoenc->framerate_counter = 0;
            hailoenc->last_timestamp = timestamp;
        }
    }
    g_queue_push_tail(hailoenc->dts_queue, GUINT_TO_POINTER(frame->pts));
}

static GstFlowReturn
gst_hailoenc_handle_frame(GstVideoEncoder *encoder,
                          GstVideoCodecFrame *frame)
{
    GstHailoEnc *hailoenc = (GstHailoEnc *)encoder;
    GstFlowReturn ret = GST_FLOW_ERROR;
    EncoderParams *enc_params = &(hailoenc->enc_params);
    GList *frames;
    guint delayed_frames;
    GstVideoCodecFrame *oldest_frame;
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);
    GST_DEBUG_OBJECT(hailoenc, "Received frame number %u", frame->system_frame_number);

    gst_hailoenc_handle_timestamps(hailoenc, frame);

    if (hailoenc->stream_restart)
    {
        ret = gst_hailoenc_stream_restart(encoder, frame);
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
        ForceKeyframe(enc_params, hailoenc->encoder_instance);
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
    case VCENC_INTRA_FRAME:
    {
        ret = encode_single_frame(hailoenc, frame);
        break;
    }
    case VCENC_PREDICTED_FRAME:
    {
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
    GST_DEBUG_OBJECT(hailoenc, "handle_frame took %lu milliseconds", (long)media_library_difftimespec_ms(end_handle, start_handle));
    return ret;
}
