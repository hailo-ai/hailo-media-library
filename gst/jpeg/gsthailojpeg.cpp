#include "gsthailojpeg.hpp"

#include <algorithm>
#include <gst/video/video.h>
#include <iostream>
#include <jpeglib.h>
#include <sstream>

GST_DEBUG_CATEGORY_STATIC(gst_hailojpegenc_debug_category);
#define GST_CAT_DEFAULT gst_hailojpegenc_debug_category

#define INNER_QUEUE_SIZE 3
#define DEFAULT_NUM_OF_THREADS 1

#define DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL 0
#define JPEG_DEFAULT_QUALITY 85
#define JPEG_DEFAULT_IDCT_METHOD JDCT_FASTEST

#define gst_hailojpegenc_parent_class parent_class

static void gst_hailojpegenc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailojpegenc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_hailojpegenc_dispose(GObject *object);
void construct_internal_pipeline(GstHailoJpegEnc *hailojpegenc);
bool link_elements(GstHailoJpegEnc *hailojpegenc);
void clear_internal_pipeline(GstHailoJpegEnc *hailojpegenc);

GType gst_idct_method_get_type(void)
{
    static GType idct_method_type = 0;
    static const GEnumValue idct_method[] = {
        {JDCT_ISLOW, "Slow but accurate integer algorithm", "islow"},
        {JDCT_IFAST, "Faster, less accurate integer method", "ifast"},
        {JDCT_FLOAT, "Floating-point: accurate, fast on fast HW", "float"},
        {0, NULL, NULL},
    };

    if (!idct_method_type)
    {
        idct_method_type = g_enum_register_static("GstHailoIDCTMethod", idct_method);
    }
    return idct_method_type;
}

enum
{
    PROP_0,
    PROP_N_THREADS,
    PROP_MIN_FORCE_KEY_UNIT_INTERVAL,
    PROP_QUALITY,
    PROP_IDCT_METHOD,
};

G_DEFINE_TYPE(GstHailoJpegEnc, gst_hailojpegenc, GST_TYPE_BIN);
GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                            GST_PAD_SRC,
                                                            GST_PAD_ALWAYS,
                                                            GST_STATIC_CAPS("image/jpeg, "
                                                                            "width = (int) [ 1, 65535 ], "
                                                                            "height = (int) [ 1, 65535 ], "
                                                                            "framerate = (fraction) [ 0/1, MAX ], "
                                                                            "sof-marker = (int) { 0, 1, 2, 4, 9 }"));

GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                             GST_PAD_SINK,
                                                             GST_PAD_ALWAYS,
                                                             GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ I420, YV12, YUY2, UYVY, Y41B, Y42B, YVYU, Y444, NV21, "
                                                                                                 "NV12, RGB, BGR, RGBx, xRGB, BGRx, xBGR, GRAY8 }")));

static void gst_hailojpegenc_class_init(GstHailoJpegEncClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = gst_hailojpegenc_set_property;
    gobject_class->get_property = gst_hailojpegenc_get_property;
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailojpegenc_dispose);

    g_object_class_install_property(gobject_class,
                                    PROP_N_THREADS,
                                    g_param_spec_uint("n-threads",
                                                      "Number of Threads",
                                                      "number of threads",
                                                      1,
                                                      4,
                                                      DEFAULT_NUM_OF_THREADS,
                                                      (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class,
                                    PROP_MIN_FORCE_KEY_UNIT_INTERVAL,
                                    g_param_spec_uint64("min-force-key-unit-interval",
                                                        "Minimum Force Keyunit Interval",
                                                        "Minimum interval between force-keyunit requests in nanoseconds", 0,
                                                        G_MAXUINT64, DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_QUALITY,
                                    g_param_spec_int("quality", "Quality", "Quality of encoding",
                                                     0, 100, JPEG_DEFAULT_QUALITY,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                                   GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_IDCT_METHOD,
                                    g_param_spec_enum("idct-method", "IDCT Method",
                                                      "The IDCT algorithm to use", GST_TYPE_IDCT_METHOD,
                                                      JPEG_DEFAULT_IDCT_METHOD,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_hailojpegenc_set_property(GObject *object, guint prop_id,
                                          const GValue *value, GParamSpec *pspec)
{
    GstHailoJpegEnc *hailojpegenc = GST_HAILOJPEGENC(object);
    GST_DEBUG_OBJECT(hailojpegenc, "set_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr))
    {
        g_error("set_property got null parameter!");
        return;
    }

    switch (prop_id)
    {
    case PROP_N_THREADS:
    {
        guint num_of_threads = g_value_get_uint(value);
        if (num_of_threads != DEFAULT_NUM_OF_THREADS)
        {
            // need to clean old pipeline that was set in init
            clear_internal_pipeline(hailojpegenc);
            hailojpegenc->num_of_threads = num_of_threads;
            construct_internal_pipeline(hailojpegenc);
        }
        break;
    }
    case PROP_MIN_FORCE_KEY_UNIT_INTERVAL:
    {
        hailojpegenc->jpegenc_min_force_key_unit_interval = g_value_get_uint64(value);
        for (auto jpegenc : hailojpegenc->m_jpegencs)
        {
            g_object_set(jpegenc, "min-force-key-unit-interval", hailojpegenc->jpegenc_min_force_key_unit_interval, NULL);
        }
        break;
    }
    case PROP_QUALITY:
    {
        hailojpegenc->jpeg_quality = g_value_get_int(value);
        for (auto jpegenc : hailojpegenc->m_jpegencs)
        {
            g_object_set(jpegenc, "quality", hailojpegenc->jpeg_quality, NULL);
        }
        break;
    }
    case PROP_IDCT_METHOD:
    {
        hailojpegenc->jpeg_idct_method = g_value_get_enum(value);
        for (auto jpegenc : hailojpegenc->m_jpegencs)
        {
            g_object_set(jpegenc, "idct-method", hailojpegenc->jpeg_idct_method, NULL);
        }
        break;
    }
    }
}

static void gst_hailojpegenc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoJpegEnc *hailojpegenc = GST_HAILOJPEGENC(object);
    GST_DEBUG_OBJECT(hailojpegenc, "get_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr))
    {
        g_error("get_property got null parameter!");
        return;
    }

    switch (property_id)
    {
    case PROP_N_THREADS:
    {
        g_value_set_uint(value, hailojpegenc->num_of_threads);
        break;
    }
    case PROP_MIN_FORCE_KEY_UNIT_INTERVAL:
    {
        g_value_set_uint64(value, hailojpegenc->jpegenc_min_force_key_unit_interval);
        break;
    }
    case PROP_QUALITY:
    {
        g_value_set_int(value, hailojpegenc->jpeg_quality);
        break;
    }
    case PROP_IDCT_METHOD:
    {
        g_value_set_enum(value, hailojpegenc->jpeg_idct_method);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void init_ghost_sink(GstHailoJpegEnc *hailojpegenc)
{
    const gchar *pad_name = "sink";
    GstPad *pad = gst_element_get_static_pad(hailojpegenc->m_roundrobin, pad_name);

    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sink_template);

    GstPad *ghost_pad = gst_ghost_pad_new_from_template("sink", pad, pad_tmpl);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailojpegenc), ghost_pad);

    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

void init_ghost_src(GstHailoJpegEnc *hailojpegenc)
{
    const gchar *pad_name = "src";

    GstPad *pad = gst_element_get_static_pad(hailojpegenc->m_hailoroundrobin, pad_name);

    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&src_template);

    GstPad *ghost_pad = gst_ghost_pad_new_from_template("src", pad, pad_tmpl);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailojpegenc), ghost_pad);

    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

bool link_elements(GstHailoJpegEnc *hailojpegenc)
{
    for (uint i = 0; i < hailojpegenc->num_of_threads; i++)
    {
        if (!gst_element_link_many(hailojpegenc->m_roundrobin, hailojpegenc->m_queues[i], hailojpegenc->m_jpegencs[i], hailojpegenc->m_hailoroundrobin, NULL))
        {
            GST_ERROR_OBJECT(hailojpegenc, "Could not add link elements in bin");
            return false;
        }
    }
    return true;
}

void clear_internal_pipeline(GstHailoJpegEnc *hailojpegenc)
{
    GST_DEBUG_OBJECT(hailojpegenc, "clear_internal_pipeline");
    for (uint i = 0; i < hailojpegenc->num_of_threads; i++)
    {
        gboolean remove_success = gst_bin_remove(GST_BIN(hailojpegenc), hailojpegenc->m_queues[i]);
        if (!remove_success)
        {
            GST_ERROR_OBJECT(hailojpegenc, "Could not remove queue element from bin");
        }

        remove_success = gst_bin_remove(GST_BIN(hailojpegenc), hailojpegenc->m_jpegencs[i]);
        if (!remove_success)
        {
            GST_ERROR_OBJECT(hailojpegenc, "Could not remove jpegenc element from bin");
        }
    }
    hailojpegenc->m_queues.clear();
    hailojpegenc->m_jpegencs.clear();
}

void construct_internal_pipeline(GstHailoJpegEnc *hailojpegenc)
{
    gchar *name;
    GST_DEBUG_OBJECT(hailojpegenc, "construct_internal_pipeline");
    for (uint i = 0; i < hailojpegenc->num_of_threads; i++)
    {
        std::stringstream ss;
        ss << "jpegenc_" << i;
        name = (gchar *)ss.str().c_str();
        GstElement *jpegenc = gst_element_factory_make("jpegenc", name);
        g_object_set(jpegenc, "min-force-key-unit-interval", hailojpegenc->jpegenc_min_force_key_unit_interval, NULL);
        g_object_set(jpegenc, "quality", hailojpegenc->jpeg_quality, NULL);
        g_object_set(jpegenc, "idct-method", hailojpegenc->jpeg_idct_method, NULL);

        // don't forget to set properties!!!!!!

        hailojpegenc->m_jpegencs.push_back(jpegenc);
        if (!gst_bin_add(GST_BIN(hailojpegenc), jpegenc))
        {
            GST_ERROR_OBJECT(hailojpegenc, "could not add jpegenc to bin");
        }

        std::stringstream ss2;
        ss2 << "queue_" << i;
        name = (gchar *)ss2.str().c_str();
        GstElement *queue = gst_element_factory_make("queue", name);
        g_object_set(queue, "max-size-buffers", INNER_QUEUE_SIZE, NULL);
        g_object_set(queue, "max-size-bytes", 0, NULL);
        g_object_set(queue, "max-size-time", 0, NULL);
        g_object_set(queue, "leaky", 0, NULL);
        hailojpegenc->m_queues.push_back(queue);
        if (!gst_bin_add(GST_BIN(hailojpegenc), queue))
        {
            GST_ERROR_OBJECT(hailojpegenc, "Could not add queue to bin");
        }
    }

    link_elements(hailojpegenc);
}

static void gst_hailojpegenc_init(GstHailoJpegEnc *hailojpegenc)
{
    hailojpegenc->num_of_threads = DEFAULT_NUM_OF_THREADS;
    hailojpegenc->jpegenc_min_force_key_unit_interval = DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL;
    hailojpegenc->jpeg_quality = JPEG_DEFAULT_QUALITY;
    hailojpegenc->jpeg_idct_method = JPEG_DEFAULT_IDCT_METHOD;

    GstElement *roundrobin = gst_element_factory_make("roundrobin", "roundrobin");
    hailojpegenc->m_roundrobin = roundrobin;
    if (!gst_bin_add(GST_BIN(hailojpegenc), hailojpegenc->m_roundrobin))
    {
        GST_ERROR_OBJECT(hailojpegenc, "Could not add roundrobin to bin");
    }

    GstElement *hailoroundrobin = gst_element_factory_make("hailoroundrobin", "hailoroundrobin");
    g_object_set(hailoroundrobin, "mode", 1, NULL);
    hailojpegenc->m_hailoroundrobin = hailoroundrobin;

    if (!gst_bin_add(GST_BIN(hailojpegenc), hailojpegenc->m_hailoroundrobin))
    {
        GST_ERROR_OBJECT(hailojpegenc, "Could not add hailoroundrobin to bin");
    }

    construct_internal_pipeline(hailojpegenc);

    init_ghost_sink(hailojpegenc);
    init_ghost_src(hailojpegenc);
}

static void
gst_hailojpegenc_dispose(GObject *object)
{
    G_OBJECT_CLASS(parent_class)->dispose(object);
}
