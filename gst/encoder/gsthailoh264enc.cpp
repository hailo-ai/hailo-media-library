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
#include "gsthailoh264enc.hpp"

/*******************
Property Definitions
*******************/
enum
{
    PROP_0,
    PROP_PROFILE,
    PROP_LEVEL,
    NUM_OF_PROPS,
};

#define GST_TYPE_HAILOH264ENC_PROFILE (gst_hailoh264enc_profile_get_type())
static GType gst_hailoh264enc_profile_get_type(void)
{
    static GType hailoh264enc_profile_type = 0;
    static const GEnumValue hailoh264enc_profiles[] = {
        {-1, "Auto Profile", "auto"},
        {VCENC_H264_BASE_PROFILE, "Base Profile", "base"},
        {VCENC_H264_MAIN_PROFILE, "Main Profile", "main"},
        {VCENC_H264_HIGH_PROFILE, "High Profile", "high"},
        {0, NULL, NULL},
    };
    if (!hailoh264enc_profile_type)
    {
        hailoh264enc_profile_type = g_enum_register_static("GstHailoH264EncProfile", hailoh264enc_profiles);
    }
    return hailoh264enc_profile_type;
}

#define GST_TYPE_HAILOH264ENC_LEVEL (gst_hailoh264enc_level_get_type())
static GType gst_hailoh264enc_level_get_type(void)
{
    static GType hailoh264enc_level_type = 0;
    static const GEnumValue hailoh264enc_levels[] = {
        {-1, "Level auto", "level-auto"},
        {VCENC_H264_LEVEL_1, "Level 1", "level-1"},
        {VCENC_H264_LEVEL_1_b, "Level 1b", "level-1-b"},
        {VCENC_H264_LEVEL_1_1, "Level 1.1", "level-1-1"},
        {VCENC_H264_LEVEL_1_2, "Level 1.2", "level-1-2"},
        {VCENC_H264_LEVEL_1_3, "Level 1.3", "level-1-3"},
        {VCENC_H264_LEVEL_2, "Level 2", "level-2"},
        {VCENC_H264_LEVEL_2_1, "Level 2.1", "level-2-1"},
        {VCENC_H264_LEVEL_2_2, "Level 2.2", "level-2-2"},
        {VCENC_H264_LEVEL_3, "Level 3", "level-3"},
        {VCENC_H264_LEVEL_3_1, "Level 3.1", "level-3-1"},
        {VCENC_H264_LEVEL_3_2, "Level 3.2", "level-3-2"},
        {VCENC_H264_LEVEL_4, "Level 4", "level-4"},
        {VCENC_H264_LEVEL_4_1, "Level 4.1", "level-4-1"},
        {VCENC_H264_LEVEL_4_2, "Level 4.2", "level-4-2"},
        {VCENC_H264_LEVEL_5, "Level 5", "level-5"},
        {VCENC_H264_LEVEL_5_1, "Level 5.1", "level-5-1"},
        {0, NULL, NULL},
    };
    if (!hailoh264enc_level_type)
    {
        hailoh264enc_level_type = g_enum_register_static("GstHailoH264EncLevel", hailoh264enc_levels);
    }
    return hailoh264enc_level_type;
}

/*******************
Function Definitions
*******************/

static void gst_hailoh264enc_class_init(GstHailoH264EncClass *klass);
static void gst_hailoh264enc_init(GstHailoH264Enc *hailoh264enc);
static void gst_hailoh264enc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoh264enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_hailoh264enc_finalize(GObject *object);
static void gst_hailoh264enc_dispose(GObject *object);

/************
Pad Templates
************/

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS("video/x-raw, "
                                                                                    "format=NV12, "
                                                                                    "width=(int)[16,MAX], "
                                                                                    "height=(int)[16,MAX], "
                                                                                    "framerate=(fraction)[0/1,MAX]"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-h264, "
                                            "stream-format = (string) byte-stream, "
                                            "alignment = (string) au, "
                                            "profile = (string) { base, main, high }"));

/*************
Init Functions
*************/

GST_DEBUG_CATEGORY_STATIC(gst_hailoh264enc_debug);
#define GST_CAT_DEFAULT gst_hailoh264enc_debug
#define _do_init GST_DEBUG_CATEGORY_INIT(gst_hailoh264enc_debug, "hailoh264enc", 0, "hailoh264enc element");
#define gst_hailoh264enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoH264Enc, gst_hailoh264enc, GST_TYPE_HAILO_ENC, _do_init);

static void gst_hailoh264enc_class_init(GstHailoH264EncClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    gobject_class = (GObjectClass *)klass;

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
    gst_element_class_set_static_metadata(element_class, "H264 Encoder", "Encoder/Video",
                                          "Encodes raw video into H264 format", "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailoh264enc_set_property;
    gobject_class->get_property = gst_hailoh264enc_get_property;

    g_object_class_install_property(gobject_class, PROP_PROFILE,
                                    g_param_spec_enum("profile", "encoder profile", "profile to encoder",
                                                      GST_TYPE_HAILOH264ENC_PROFILE, (gint)DEFAULT_H264_PROFILE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_LEVEL,
                                    g_param_spec_enum("level", "encoder level", "level to encoder",
                                                      GST_TYPE_HAILOH264ENC_LEVEL, (gint)DEFAULT_H264_LEVEL,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gobject_class->finalize = gst_hailoh264enc_finalize;
    gobject_class->dispose = gst_hailoh264enc_dispose;
}

static void gst_hailoh264enc_init(GstHailoH264Enc *hailoh264enc)
{
    GstHailoEnc *hailoenc = GST_HAILO_ENC(hailoh264enc);
    EncoderParams *enc_params = &(hailoenc->enc_params);
    GST_PAD_SET_ACCEPT_TEMPLATE(GST_VIDEO_ENCODER_SINK_PAD(hailoenc));
    SetDefaultParameters(enc_params, true);
}

/************************
GObject Virtual Functions
************************/

static void gst_hailoh264enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstHailoH264Enc *hailoh264enc = (GstHailoH264Enc *)(object);
    GstHailoEnc *hailoenc = (GstHailoEnc *)(object);

    switch (prop_id)
    {
    case PROP_PROFILE:
        GST_OBJECT_LOCK(hailoh264enc);
        g_value_set_enum(value, (gint)hailoenc->enc_params.profile);
        GST_OBJECT_UNLOCK(hailoh264enc);
        break;
    case PROP_LEVEL:
        GST_OBJECT_LOCK(hailoh264enc);
        g_value_set_enum(value, (gint)hailoenc->enc_params.level);
        GST_OBJECT_UNLOCK(hailoh264enc);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_hailoh264enc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoH264Enc *hailoh264enc = (GstHailoH264Enc *)(object);
    GstHailoEnc *hailoenc = (GstHailoEnc *)(object);

    switch (prop_id)
    {
    case PROP_PROFILE:
        GST_OBJECT_LOCK(hailoh264enc);
        hailoenc->enc_params.profile = g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoh264enc);
        break;
    case PROP_LEVEL:
        GST_OBJECT_LOCK(hailoh264enc);
        hailoenc->enc_params.level = g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoh264enc);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_hailoh264enc_finalize(GObject *object)
{
    GstHailoH264Enc *hailoh264enc = (GstHailoH264Enc *)object;
    GST_DEBUG_OBJECT(hailoh264enc, "hailoh264enc finalize callback");

    /* clean up remaining allocated data */
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_hailoh264enc_dispose(GObject *object)
{
    GstHailoH264Enc *hailoh264enc = (GstHailoH264Enc *)object;
    GST_DEBUG_OBJECT(hailoh264enc, "hailoh264enc dispose callback");

    /* clean up as possible.  may be called multiple times */
    G_OBJECT_CLASS(parent_class)->dispose(object);
}
