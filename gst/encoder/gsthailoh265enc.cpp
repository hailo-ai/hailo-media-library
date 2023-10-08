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
#include "gsthailoh265enc.hpp"


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

#define GST_TYPE_HAILOH265ENC_PROFILE (gst_hailoh265enc_profile_get_type())
static GType
gst_hailoh265enc_profile_get_type(void)
{
    static GType hailoh265enc_profile_type = 0;
    static const GEnumValue hailoh265enc_profiles[] = {
        {VCENC_HEVC_MAIN_PROFILE, "Main Profile", "main"},
        {VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE, "Main Still Picture Profile", "main-still-picture"},
        {VCENC_HEVC_MAIN_10_PROFILE, "Main 10 Profile", "main-10"},
        {0, NULL, NULL},
    };
    if (!hailoh265enc_profile_type)
    {
        hailoh265enc_profile_type =
            g_enum_register_static("GstHailoH265EncProfile", hailoh265enc_profiles);
    }
    return hailoh265enc_profile_type;
}

#define GST_TYPE_HAILOH265ENC_LEVEL (gst_hailoh265enc_level_get_type())
static GType
gst_hailoh265enc_level_get_type(void)
{
    static GType hailoh265enc_level_type = 0;
    static const GEnumValue hailoh265enc_levels[] = {
        {VCENC_HEVC_LEVEL_1, "Level 1", "level-1"},
        {VCENC_HEVC_LEVEL_2, "Level 2", "level-2"},
        {VCENC_HEVC_LEVEL_2_1, "Level 2.1", "level-2-1"},
        {VCENC_HEVC_LEVEL_3, "Level 3", "level-3"},
        {VCENC_HEVC_LEVEL_3_1, "Level 3.1", "level-3-1"},
        {VCENC_HEVC_LEVEL_4, "Level 4", "level-4"},
        {VCENC_HEVC_LEVEL_4_1, "Level 4.1", "level-4-1"},
        {VCENC_HEVC_LEVEL_5, "Level 5", "level-5"},
        {VCENC_HEVC_LEVEL_5_1, "Level 5.1", "level-5-1"},
        {0, NULL, NULL},
    };
    if (!hailoh265enc_level_type)
    {
        hailoh265enc_level_type =
            g_enum_register_static("GstHailoH265EncLevel", hailoh265enc_levels);
    }
    return hailoh265enc_level_type;
}

/*******************
Function Definitions
*******************/

static void gst_hailoh265enc_class_init(GstHailoH265EncClass * klass);
static void gst_hailoh265enc_init(GstHailoH265Enc * hailoh265enc);
static void gst_hailoh265enc_set_property(GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_hailoh265enc_get_property(GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_hailoh265enc_finalize(GObject * object);
static void gst_hailoh265enc_dispose(GObject *object);

/************
Pad Templates
************/

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, "
        "format=NV12, "
        "width=(int)[16,MAX], "
        "height=(int)[16,MAX], "
        "framerate=(fraction)[0/1,MAX]"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h265, "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "profile = (string) { main, main-still-picture, main-intra, main-10, main-10-intra }"));

/*************
Init Functions
*************/

GST_DEBUG_CATEGORY_STATIC(gst_hailoh265enc_debug);
#define GST_CAT_DEFAULT gst_hailoh265enc_debug
#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_hailoh265enc_debug, "hailoh265enc", 0, "hailoh265enc element");
#define gst_hailoh265enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoH265Enc, gst_hailoh265enc, GST_TYPE_HAILO_ENC, _do_init);

static void
gst_hailoh265enc_class_init(GstHailoH265EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  gobject_class = (GObjectClass *) klass;

  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get(&sink_template));
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get(&src_template));
  gst_element_class_set_static_metadata(element_class,
                                        "H265 Encoder",
                                        "Encoder/Video",
                                        "Encodes raw video into H265 format",
                                        "hailo.ai <contact@hailo.ai>");

  gobject_class->set_property = gst_hailoh265enc_set_property;
  gobject_class->get_property = gst_hailoh265enc_get_property;

  g_object_class_install_property(gobject_class, PROP_PROFILE,
                                    g_param_spec_enum("profile", "encoder profile", "profile to encoder",
                                                      GST_TYPE_HAILOH265ENC_PROFILE, (gint)DEFAULT_HEVC_PROFILE,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_LEVEL,
                                    g_param_spec_enum("level", "encoder level", "level to encoder",
                                                      GST_TYPE_HAILOH265ENC_LEVEL, (gint)DEFAULT_HEVC_LEVEL,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
                              
  gobject_class->finalize = gst_hailoh265enc_finalize;
  gobject_class->dispose = gst_hailoh265enc_dispose;

}

static void
gst_hailoh265enc_init(GstHailoH265Enc * hailoh265enc)
{
  GstHailoEnc *hailoenc = GST_HAILO_ENC(hailoh265enc);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (hailoh265enc));
  EncoderParams *enc_params = &(hailoenc->enc_params);
  SetDefaultParameters(enc_params, false);
}

/************************
GObject Virtual Functions
************************/

static void
gst_hailoh265enc_get_property(GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstHailoH265Enc *hailoh265enc = (GstHailoH265Enc *) (object);
  GstHailoEnc *hailoenc = (GstHailoEnc *) (object);

  switch (prop_id) {
    case PROP_PROFILE:
        GST_OBJECT_LOCK(hailoh265enc);
        g_value_set_enum(value, (gint)hailoenc->enc_params.profile);
        GST_OBJECT_UNLOCK(hailoh265enc);
        break;
    case PROP_LEVEL:
        GST_OBJECT_LOCK(hailoh265enc);
        g_value_set_enum(value, (gint)hailoenc->enc_params.level);
        GST_OBJECT_UNLOCK(hailoh265enc);
        break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hailoh265enc_set_property(GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstHailoH265Enc *hailoh265enc = (GstHailoH265Enc *) (object);
  GstHailoEnc *hailoenc = (GstHailoEnc *) (object);

  switch (prop_id) {
    case PROP_PROFILE:
        GST_OBJECT_LOCK(hailoh265enc);
        hailoenc->enc_params.profile = (VCEncProfile)g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoh265enc);
        break;
    case PROP_LEVEL:
        GST_OBJECT_LOCK(hailoh265enc);
        hailoenc->enc_params.level = (VCEncLevel)g_value_get_enum(value);
        GST_OBJECT_UNLOCK(hailoh265enc);
        break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hailoh265enc_finalize(GObject * object)
{
  GstHailoH265Enc *hailoh265enc = (GstHailoH265Enc *) object;
  GST_DEBUG_OBJECT(hailoh265enc, "hailoh265enc finalize callback");

  /* clean up remaining allocated data */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_hailoh265enc_dispose(GObject * object)
{
  GstHailoH265Enc *hailoh265enc = (GstHailoH265Enc *) object;
  GST_DEBUG_OBJECT(hailoh265enc, "hailoh265enc dispose callback");

  /* clean up as possible.  may be called multiple times */
  G_OBJECT_CLASS (parent_class)->dispose (object);
}