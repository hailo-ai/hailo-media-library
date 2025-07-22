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

#include "gsthailoencodebin.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "media_library/media_library_types.hpp"
#include "common/gstmedialibcommon.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailoencodebin_debug_category);
#define GST_CAT_DEFAULT gst_hailoencodebin_debug_category

static void gst_hailoencodebin_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoencodebin_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_hailoencodebin_init_ghost_sink(GstHailoEncodeBin *hailoencodebin);
static void gst_hailoencodebin_init_ghost_src(GstHailoEncodeBin *hailoencodebin);
static EncoderType gst_hailoencodebin_get_encoder_type(nlohmann::json &config_json);
static nlohmann::json gst_hailoencodebin_get_encoder_json(const gchar *property_value, bool is_file);
static void gst_hailoencodebin_set_encoder_properties(GstHailoEncodeBin *hailoencodebin, const char *config_property,
                                                      const gchar *property_value);
static gboolean gst_hailoencodebin_prepare_encoder_element(GstHailoEncodeBin *hailoencodebin,
                                                           const char *config_property, const gchar *property_value);
static gboolean gst_hailoencodebin_link_elements(GstElement *element);
static void gst_hailoencodebin_dispose(GObject *object);

typedef enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_CONFIG,
    PROP_WAIT_FOR_WRITABLE_BUFFER,
    PROP_OSD_BLENDER,
    PROP_PM_BLENDER,
    PROP_QUEUE_SIZE,
    PROP_ENFORCE_CAPS,
    PROP_USER_CONFIG,
    PROP_ENCODER_MONITORS,
} hailoencodebin_prop_t;

// Pad Templates
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoEncodeBin, gst_hailoencodebin, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailoencodebin_debug_category, "hailoencodebin", 0,
                                                "debug category for hailoencodebin element"));

static void gst_hailoencodebin_class_init(GstHailoEncodeBinClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class, "Hailo Encode Bin", "Hailo/Media-Library",
                                          "Encode Bin for vision pipelines.", "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_hailoencodebin_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_hailoencodebin_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailoencodebin_dispose);

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_FILE_PATH,
        g_param_spec_string("config-file-path", "Config file path, cannot be used with other properties",
                            "JSON config file path to load", "",
                            (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                          GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_STRING,
        g_param_spec_string("config-string", "Config string, cannot be used with other properties",
                            "JSON config string to load", "",
                            (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                          GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_WAIT_FOR_WRITABLE_BUFFER,
        g_param_spec_boolean("wait-for-writable-buffer", "Wait for writable buffer",
                             "Enables the element thread to wait until incoming buffer is writable", FALSE,
                             (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                           GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_OSD_BLENDER,
                                    g_param_spec_pointer("osd-blender", "OSD blender object",
                                                         "Pointer to OSD blender object",
                                                         (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READABLE)));

    g_object_class_install_property(gobject_class, PROP_PM_BLENDER,
                                    g_param_spec_pointer("privacy-mask-blender", "Privacy mask blender object",
                                                         "Pointer to privacy mask blender object",
                                                         (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READABLE)));

    g_object_class_install_property(
        gobject_class, PROP_QUEUE_SIZE,
        g_param_spec_uint("queue-size", "Queue size", "Size of the queues in the bin, there are 2 queues.",
                          MIN_QUEUE_SIZE, G_MAXUINT, ENCODEBIN_DEFAULT_QUEUE_SIZE,
                          (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                        GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG,
        g_param_spec_pointer("config", "Config", "Pointer to the actual config object",
                             (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_USER_CONFIG,
        g_param_spec_pointer("user-config", "User Config", "Pointer to the user config object",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_ENFORCE_CAPS,
        g_param_spec_boolean("enforce-caps", "Enforece caps", "Enforce caps on the input/output pad of the bin", TRUE,
                             (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                           GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_ENCODER_MONITORS,
        g_param_spec_pointer("encoder-monitors", "Encoder Monitors", "Struct that holds the encoder monitors",
                             (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
}

static void gst_hailoencodebin_init(GstHailoEncodeBin *hailoencodebin)
{
    hailoencodebin->params = new GstHailoEncodeBinParams();

    // Prepare internal elements
    // osd
    hailoencodebin->params->m_osd = gst_element_factory_make("hailoosd", NULL);
    if (nullptr == hailoencodebin->params->m_osd)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating hailoosd element in bin!"), (NULL));
    }

    // queue between osd and encoder
    hailoencodebin->params->m_queue_encoder = gst_element_factory_make("queue", NULL);
    if (nullptr == hailoencodebin->params->m_queue_encoder)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
    }
    // Passing 0 disables the features here
    g_object_set(hailoencodebin->params->m_queue_encoder, "max-size-time", (guint64)0, NULL);
    g_object_set(hailoencodebin->params->m_queue_encoder, "max-size-bytes", (guint)0, NULL);
    g_object_set(hailoencodebin->params->m_queue_encoder, "max-size-buffers", (guint)hailoencodebin->params->queue_size,
                 NULL);

    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailoencodebin), hailoencodebin->params->m_osd, hailoencodebin->params->m_queue_encoder,
                     NULL);
    gst_hailoencodebin_init_ghost_sink(hailoencodebin);
}

void gst_hailoencodebin_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoEncodeBin *hailoencodebin = GST_HAILO_ENCODE_BIN(object);
    GST_DEBUG_OBJECT(hailoencodebin, "set_property");

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH: {
        hailoencodebin->params->config_file_path = glib_cpp::get_string_from_gvalue(value);
        GST_DEBUG_OBJECT(hailoencodebin, "config_file_path: %s", hailoencodebin->params->config_file_path.c_str());

        nlohmann::json config_json = gst_hailoencodebin_get_encoder_json(g_value_get_string(value), true);
        std::string config_json_str = config_json.dump();

        // set params for sub elements here
        g_object_set(hailoencodebin->params->m_osd, "config-string", config_json_str.c_str(), NULL);
        if (hailoencodebin->params->m_encoder)
        {
            EncoderType encoder_type = gst_hailoencodebin_get_encoder_type(config_json);

            if (hailoencodebin->params->encoder_type == encoder_type)
            {
                gst_hailoencodebin_set_encoder_properties(hailoencodebin, "config-string", config_json_str.c_str());
            }
            else
            {
                GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED,
                                  ("Changing encoder types after encoder element is created is not allowed"), (NULL));
            }
        }

        // Now that configuration is known, link the elements
        if (hailoencodebin->params->m_elements_linked == FALSE)
        {
            if (!hailoencodebin->params->m_encoder)
            {
                gst_hailoencodebin_prepare_encoder_element(hailoencodebin, "config-file-path",
                                                           g_value_get_string(value));
            }
            if (gst_hailoencodebin_link_elements(GST_ELEMENT(hailoencodebin)))
            {
                hailoencodebin->params->m_elements_linked = TRUE;
            }
        }
        break;
    }
    case PROP_CONFIG_STRING: {
        hailoencodebin->params->config_string = std::string(g_value_get_string(value));
        GST_DEBUG_OBJECT(hailoencodebin, "config-string: %s", hailoencodebin->params->config_string.c_str());

        // set params for sub elements here
        g_object_set(hailoencodebin->params->m_osd, "config-string", g_value_get_string(value), NULL);
        if (hailoencodebin->params->m_encoder)
        {
            nlohmann::json config_json = gst_hailoencodebin_get_encoder_json(g_value_get_string(value), false);
            EncoderType encoder_type = gst_hailoencodebin_get_encoder_type(config_json);
            if (hailoencodebin->params->encoder_type == encoder_type)
            {
                gst_hailoencodebin_set_encoder_properties(hailoencodebin, "config-string", g_value_get_string(value));
            }
            else
            {
                GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED,
                                  ("Changing encoder types after encoder element is created is not allowed"), (NULL));
            }
        }

        // Now that configuration is known, link the elements
        if (hailoencodebin->params->m_elements_linked == FALSE)
        {
            if (!hailoencodebin->params->m_encoder)
            {
                gst_hailoencodebin_prepare_encoder_element(hailoencodebin, "config-string", g_value_get_string(value));
            }
            if (gst_hailoencodebin_link_elements(GST_ELEMENT(hailoencodebin)))
            {
                hailoencodebin->params->m_elements_linked = TRUE;
            }
        }
        break;
    }
    case PROP_USER_CONFIG: {
        gpointer config = g_value_get_pointer(value);
        g_object_set(hailoencodebin->params->m_encoder, "user-config", config, NULL);
        break;
    }
    case PROP_WAIT_FOR_WRITABLE_BUFFER: {
        gboolean wait_for_writable_buffer = g_value_get_boolean(value);
        g_object_set(hailoencodebin->params->m_osd, "wait-for-writable-buffer", wait_for_writable_buffer, NULL);
        break;
    }
    case PROP_ENFORCE_CAPS: {
        gboolean enforce_caps = g_value_get_boolean(value);
        g_object_set(hailoencodebin->params->m_encoder, "enforce-caps", enforce_caps, NULL);
        break;
    }
    case PROP_QUEUE_SIZE: {
        hailoencodebin->params->queue_size = g_value_get_uint(value);
        g_object_set(hailoencodebin->params->m_queue_encoder, "max-size-buffers", hailoencodebin->params->queue_size,
                     NULL);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoencodebin_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoEncodeBin *hailoencodebin = GST_HAILO_ENCODE_BIN(object);
    GST_DEBUG_OBJECT(hailoencodebin, "get_property");
    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH: {
        g_value_set_string(value, hailoencodebin->params->config_file_path.c_str());
        break;
    }
    case PROP_CONFIG_STRING: {
        g_value_set_string(value, hailoencodebin->params->config_string.c_str());
        break;
    }
    case PROP_CONFIG: {
        if (hailoencodebin->params->m_encoder == NULL)
        {
            g_value_set_pointer(value, NULL);
            break;
        }
        gpointer config;
        g_object_get(hailoencodebin->params->m_encoder, "config", &config, NULL);
        g_value_set_pointer(value, config);
        break;
    }
    case PROP_USER_CONFIG: {
        if (hailoencodebin->params->m_encoder == NULL)
        {
            g_value_set_pointer(value, NULL);
            break;
        }
        gpointer config;
        g_object_get(hailoencodebin->params->m_encoder, "user-config", &config, NULL);
        g_value_set_pointer(value, config);
        break;
    }
    case PROP_WAIT_FOR_WRITABLE_BUFFER: {
        gboolean wait_for_writable_buffer;
        g_object_get(hailoencodebin->params->m_osd, "wait-for-writable-buffer", &wait_for_writable_buffer, NULL);
        g_value_set_boolean(value, wait_for_writable_buffer);
        break;
    }
    case PROP_OSD_BLENDER: {
        gpointer osd_blender;
        g_object_get(hailoencodebin->params->m_osd, "osd-blender", &osd_blender, NULL);
        g_value_set_pointer(value, osd_blender);
        break;
    }
    case PROP_PM_BLENDER: {
        gpointer pm_blender;
        g_object_get(hailoencodebin->params->m_osd, "privacy-mask-blender", &pm_blender, NULL);
        g_value_set_pointer(value, pm_blender);
        break;
    }
    case PROP_QUEUE_SIZE: {
        g_value_set_uint(value, hailoencodebin->params->queue_size);
        break;
    }
    case PROP_ENFORCE_CAPS: {
        if (hailoencodebin->params->m_encoder == NULL)
        {
            g_value_set_boolean(value, true);
            break;
        }
        gboolean enforce;
        g_object_get(hailoencodebin->params->m_encoder, "enforce-caps", &enforce, NULL);
        g_value_set_boolean(value, enforce);
        break;
    }
    case PROP_ENCODER_MONITORS: {
        if (hailoencodebin->params->m_encoder == NULL)
        {
            g_value_set_pointer(value, NULL);
            break;
        }
        gpointer encoder_monitors;
        g_object_get(hailoencodebin->params->m_encoder, "encoder-monitors", &encoder_monitors, NULL);
        g_value_set_pointer(value, encoder_monitors);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoencodebin_init_ghost_sink(GstHailoEncodeBin *hailoencodebin)
{
    // Get the connecting pad
    const gchar *pad_name = "sink";
    GstPadPtr pad = gst_element_get_static_pad(hailoencodebin->params->m_osd, pad_name);

    // Create a ghostpad and connect it to the bin
    GstPadTemplatePtr pad_tmpl = gst_static_pad_template_get(&sink_template);
    hailoencodebin->params->sinkpad = gst_ghost_pad_new_from_template(pad_name, pad, pad_tmpl);
    gst_pad_set_active(hailoencodebin->params->sinkpad, TRUE);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(hailoencodebin), hailoencodebin->params->sinkpad);
}

void gst_hailoencodebin_init_ghost_src(GstHailoEncodeBin *hailoencodebin)
{
    // Get the connecting pad
    const gchar *pad_name = "src";
    GstPadPtr pad = gst_element_get_static_pad(hailoencodebin->params->m_encoder, pad_name);

    // Create a ghostpad and connect it to the bin
    GstPadTemplatePtr pad_tmpl = gst_static_pad_template_get(&src_template);
    hailoencodebin->params->srcpad = gst_ghost_pad_new_from_template(pad_name, pad, pad_tmpl);
    gst_pad_set_active(hailoencodebin->params->srcpad, TRUE);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(hailoencodebin), hailoencodebin->params->srcpad);
}

static const char *gst_hailoencodebin_get_encoder_element_name(const EncoderType encoder_type)
{
    switch (encoder_type)
    {
    case EncoderType::Hailo:
        return "hailoencoder";
    case EncoderType::Jpeg:
        return "hailojpegenc";
    default:
        return nullptr;
    }
}

static EncoderType gst_hailoencodebin_get_encoder_type(nlohmann::json &config_json)
{
    if (config_json.is_discarded() || !config_json.contains("encoding"))
    {
        return EncoderType::None;
    }

    if (config_json["encoding"].contains("jpeg_encoder"))
    {
        return EncoderType::Jpeg;
    }

    if (config_json["encoding"].contains("hailo_encoder"))
    {
        return EncoderType::Hailo;
    }

    return EncoderType::None;
}

static nlohmann::json gst_hailoencodebin_get_encoder_json(const gchar *property_value, bool is_file)
{
    std::string config;

    if (is_file)
    {
        config = gstmedialibcommon::read_json_string_from_file(property_value);
    }
    else
    {
        config = property_value;
        gstmedialibcommon::strip_string_syntax(config);
    }

    return nlohmann::json::parse(config, nullptr, false);
}

static void gst_hailoencodebin_set_encoder_properties(GstHailoEncodeBin *hailoencodebin, const char *config_property,
                                                      const gchar *property_value)
{
    switch (hailoencodebin->params->encoder_type)
    {
    case EncoderType::Hailo:
    case EncoderType::Jpeg: {
        g_object_set(hailoencodebin->params->m_encoder, config_property, property_value, NULL);
        break;
    }
    case EncoderType::None:
        // Should not reach here
        break;
    }
}

static gboolean gst_hailoencodebin_prepare_encoder_element(GstHailoEncodeBin *hailoencodebin,
                                                           const char *config_property, const gchar *property_value)
{
    bool is_file = strcmp(config_property, "config-file-path") == 0;
    nlohmann::json config_json = gst_hailoencodebin_get_encoder_json(property_value, is_file);
    EncoderType encoder_type = gst_hailoencodebin_get_encoder_type(config_json);
    const char *encoder_element_name = gst_hailoencodebin_get_encoder_element_name(encoder_type);
    if (encoder_element_name == nullptr)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("No encoder found in config json"), (NULL));
        return FALSE;
    }

    hailoencodebin->params->m_encoder = gst_element_factory_make(encoder_element_name, NULL);
    if (nullptr == hailoencodebin->params->m_encoder)
    {
        GST_ELEMENT_ERROR(hailoencodebin, RESOURCE, FAILED, ("Failed creating hailoencoder element in bin!"), (NULL));
        return FALSE;
    }

    hailoencodebin->params->encoder_type = encoder_type;
    gst_hailoencodebin_set_encoder_properties(hailoencodebin, config_property, property_value);
    gst_bin_add(GST_BIN(hailoencodebin), hailoencodebin->params->m_encoder);
    // Now that we have encoder, initialize the ghost src pad
    gst_hailoencodebin_init_ghost_src(hailoencodebin);
    return TRUE;
}

static gboolean gst_hailoencodebin_link_elements(GstElement *element)
{
    GstHailoEncodeBin *self = GST_HAILO_ENCODE_BIN(element);

    // Link the elements
    gboolean link_status =
        gst_element_link_many(self->params->m_osd, self->params->m_queue_encoder, self->params->m_encoder, NULL);
    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "Failed to link elements in bin!");
        return FALSE;
    }

    return TRUE;
}

static void gst_hailoencodebin_dispose(GObject *object)
{
    GstHailoEncodeBin *self = GST_HAILO_ENCODE_BIN(object);
    GST_DEBUG_OBJECT(self, "dispose");
    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(gst_hailoencodebin_parent_class)->dispose(object);
}
