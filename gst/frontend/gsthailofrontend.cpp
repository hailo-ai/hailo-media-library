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

#include "gsthailofrontend.hpp"
#include "multi_resize/gsthailomultiresize.hpp"
#include "media_library/privacy_mask.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailofrontend_debug_category);
#define GST_CAT_DEFAULT gst_hailofrontend_debug_category

static void gst_hailofrontend_set_property(GObject *object,
                                           guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailofrontend_get_property(GObject *object,
                                           guint property_id, GValue *value, GParamSpec *pspec);
static GstElement *gst_hailofrontend_init_queue(GstHailoFrontend *hailofrontend, bool leaky);
static GstStateChangeReturn gst_hailofrontend_change_state(GstElement *element, GstStateChange transition);
static void gst_hailofrontend_init_ghost_sink(GstHailoFrontend *hailofrontend);
static GstPad *gst_hailofrontend_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps);
static void gst_hailofrontend_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailofrontend_link_elements(GstElement *element);
static void gst_hailofrontend_dispose(GObject *object);
static void gst_hailofrontend_reset(GstHailoFrontend *self);

enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_PRIVACY_MASK,
};

// Pad Templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src_%u",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_REQUEST,
                                                                   GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoFrontend, gst_hailofrontend, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailofrontend_debug_category, "hailofrontend", 0,
                                                "debug category for hailofrontend element"));

static void
gst_hailofrontend_class_init(GstHailoFrontendClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class,
                                          "frontend vision pipeline", "Hailo/Media-Library", "Frontend bin for vision pipelines.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_hailofrontend_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_hailofrontend_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailofrontend_dispose);

    g_object_class_install_property(gobject_class, PROP_CONFIG_FILE_PATH,
                                    g_param_spec_string("config-file-path", "Config file path",
                                                        "JSON config file path to load",
                                                        "",
                                                        (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_CONFIG_STRING,
                                    g_param_spec_string("config-string", "Config string",
                                                        "JSON config string to load",
                                                        "",
                                                        (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_PRIVACY_MASK,
                                    g_param_spec_pointer("privacy-mask", "Privacy Mask",
                                                         "Pointer to privacy mask blender",
                                                         (GParamFlags)(G_PARAM_READABLE)));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailofrontend_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailofrontend_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailofrontend_release_pad);
}

static void
gst_hailofrontend_init(GstHailoFrontend *hailofrontend)
{
    // Default values
    hailofrontend->config_file_path = NULL;
    hailofrontend->srcpads = {};
    hailofrontend->m_elements_linked = FALSE;

    // Prepare internal elements
    // denoise
    hailofrontend->m_denoise = gst_element_factory_make("hailodenoise", NULL);
    if (nullptr == hailofrontend->m_denoise)
    {
        GST_ELEMENT_ERROR(hailofrontend, RESOURCE, FAILED, ("Failed creating hailodenoise element in bin!"), (NULL));
    }

    // queue between denoise and dis_dewarp
    hailofrontend->m_denoise_dis_queue = gst_hailofrontend_init_queue(hailofrontend, true);

    // dis_dewarp
    hailofrontend->m_dis_dewarp = gst_element_factory_make("hailodewarp", NULL);
    if (nullptr == hailofrontend->m_dis_dewarp)
    {
        GST_ELEMENT_ERROR(hailofrontend, RESOURCE, FAILED, ("Failed creating hailodewarp element in bin!"), (NULL));
    }

    // queue between dewarp and multi_resize
    hailofrontend->m_dewarp_mresize_queue = gst_hailofrontend_init_queue(hailofrontend, false);

    // multi_resize
    hailofrontend->m_multi_resize = gst_element_factory_make("hailomultiresize", NULL);
    if (nullptr == hailofrontend->m_multi_resize)
    {
        GST_ELEMENT_ERROR(hailofrontend, RESOURCE, FAILED, ("Failed creating hailomultiresize element in bin!"), (NULL));
    }

    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailofrontend),
                     hailofrontend->m_denoise,
                     hailofrontend->m_denoise_dis_queue,
                     hailofrontend->m_dis_dewarp,
                     hailofrontend->m_dewarp_mresize_queue,
                     hailofrontend->m_multi_resize, NULL);
}

static GstElement *
gst_hailofrontend_init_queue(GstHailoFrontend *hailofrontend, bool leaky)
{
    // queue between dewarp and multi_resize
    GstElement *queue = gst_element_factory_make("queue", NULL);
    if (nullptr == queue)
    {
        GST_ELEMENT_ERROR(hailofrontend, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
        return NULL;
    }
    // Passing 0 disables the features here
    g_object_set(queue, "max-size-time", (guint64)0, NULL);
    g_object_set(queue, "max-size-bytes", (guint)0, NULL);
    g_object_set(queue, "max-size-buffers", (guint)1, NULL);
    // Upon request, enable leaky queue (downstream)
    if (leaky)
    {
        g_object_set(queue, "leaky", (guint)2, NULL);
    }
    return queue;
}

void gst_hailofrontend_set_property(GObject *object, guint property_id,
                                    const GValue *value, GParamSpec *pspec)
{
    GstHailoFrontend *hailofrontend = GST_HAILO_FRONTEND(object);
    GST_DEBUG_OBJECT(hailofrontend, "set_property");
    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        hailofrontend->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(hailofrontend, "config_file_path: %s", hailofrontend->config_file_path);

        // set params for sub elements here
        g_object_set(hailofrontend->m_denoise, "config-file-path", g_value_get_string(value), NULL);
        g_object_set(hailofrontend->m_dis_dewarp, "config-file-path", g_value_get_string(value), NULL);
        g_object_set(hailofrontend->m_multi_resize, "config-file-path", g_value_get_string(value), NULL);

        // Now that configuration is known, link the elements
        if (hailofrontend->m_elements_linked == FALSE)
        {
            if (gst_hailofrontend_link_elements(GST_ELEMENT(hailofrontend)))
            {
                hailofrontend->m_elements_linked = TRUE;
            }
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        hailofrontend->config_string = std::string(g_value_get_string(value));
        GST_DEBUG_OBJECT(hailofrontend, "config-string: %s", hailofrontend->config_string.c_str());

        // set params for sub elements here
        g_object_set(hailofrontend->m_denoise, "config-string", g_value_get_string(value), NULL);
        g_object_set(hailofrontend->m_dis_dewarp, "config-string", g_value_get_string(value), NULL);
        g_object_set(hailofrontend->m_multi_resize, "config-string", g_value_get_string(value), NULL);

        // Now that configuration is known, link the elements
        if (hailofrontend->m_elements_linked == FALSE)
        {
            if (gst_hailofrontend_link_elements(GST_ELEMENT(hailofrontend)))
            {
                hailofrontend->m_elements_linked = TRUE;
            }
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailofrontend_get_property(GObject *object, guint property_id,
                                    GValue *value, GParamSpec *pspec)
{
    GstHailoFrontend *hailofrontend = GST_HAILO_FRONTEND(object);
    GST_DEBUG_OBJECT(hailofrontend, "get_property");
    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, hailofrontend->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailofrontend->config_string.c_str());
        break;
    }
    case PROP_PRIVACY_MASK:
    {
        gpointer blender;
        g_object_get(hailofrontend->m_multi_resize, "privacy-mask", &blender, NULL);
        g_value_set_pointer(value, blender);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstStateChangeReturn
gst_hailofrontend_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
    GstHailoFrontend *self = GST_HAILO_FRONTEND(element);
    result = GST_ELEMENT_CLASS(gst_hailofrontend_parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_NULL_TO_READY");
        if (self->m_elements_linked == FALSE)
        {
            GST_ERROR_OBJECT(self, "Elements are not linked!");
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_PAUSED");
        break;
    }
    default:
        break;
    }

    return result;
}

void gst_hailofrontend_init_ghost_sink(GstHailoFrontend *hailofrontend)
{
    // Get the connecting pad
    GstPad *pad = gst_element_get_static_pad(hailofrontend->m_denoise, "sink");
    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sink_template);
    hailofrontend->sinkpad = gst_ghost_pad_new_from_template("sink", pad, pad_tmpl);
    gst_pad_set_active(hailofrontend->sinkpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailofrontend), hailofrontend->sinkpad);

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

static GstPad *
gst_hailofrontend_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps)
{
    GstPad *srcpad;
    GstHailoFrontend *self = GST_HAILO_FRONTEND(element);
    GST_DEBUG_OBJECT(self, "Frontend request new pad name: %s", name);

    // Get the source pad from GstHailoMultiResize that you want to expose
    GstPad *multi_resize_srcpad = gst_element_request_pad(self->m_multi_resize, templ, name, caps);
    GST_DEBUG_OBJECT(self, "Frontend requested multi_resize_srcpad: %s", GST_PAD_NAME(multi_resize_srcpad));

    // Create a new ghost pad and target GstHailoMultiResize source pad
    srcpad = gst_ghost_pad_new_no_target(NULL, GST_PAD_SRC);
    gboolean link_status = gst_ghost_pad_set_target(GST_GHOST_PAD(srcpad), multi_resize_srcpad);
    GST_DEBUG_OBJECT(self, "Frontend setting %s to target %s", GST_PAD_NAME(srcpad), GST_PAD_NAME(multi_resize_srcpad));
    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "Frontend failed to set %s to target %s", GST_PAD_NAME(srcpad), GST_PAD_NAME(multi_resize_srcpad));
    }

    // Unreference the multi_resize_srcpad when you're done with it
    gst_object_unref(multi_resize_srcpad);

    // Set the new ghostpad to active and add it to the bin
    gst_pad_set_active(srcpad, TRUE);
    gst_element_add_pad(element, srcpad);
    self->srcpads.emplace_back(srcpad);

    return srcpad;
}

static void
gst_hailofrontend_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoFrontend *self = GST_HAILO_FRONTEND(element);
    gchar *name = gst_pad_get_name(pad);
    GST_DEBUG_OBJECT(self, "Release pad: %s", name);

    GST_OBJECT_LOCK(self);

    // Find the corresponding source pad in GstHailoMultiResize
    GstPad *multi_resize_srcpad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));

    // Release the source pad in GstHailoMultiResize
    gst_element_release_request_pad(self->m_multi_resize, multi_resize_srcpad);

    // Remove the ghost pad from GstHailoFrontend
    gst_element_remove_pad(element, pad);

    GST_OBJECT_UNLOCK(self);

    // Unreference the multi_resize_srcpad when you're done with it
    gst_object_unref(multi_resize_srcpad);
}

static gboolean
gst_hailofrontend_link_elements(GstElement *element)
{
    GstHailoFrontend *self = GST_HAILO_FRONTEND(element);

    // Initialize the ghost sink pad
    gst_hailofrontend_init_ghost_sink(self);

    // Link the elements
    gboolean link_status = gst_element_link_many(self->m_denoise,
                                                 self->m_denoise_dis_queue,
                                                 self->m_dis_dewarp,
                                                 self->m_dewarp_mresize_queue,
                                                 self->m_multi_resize,
                                                 NULL);

    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "Failed to link elements in bin!");
        return FALSE;
    }

    return TRUE;
}

static void
gst_hailofrontend_dispose(GObject *object)
{
    GstHailoFrontend *self = GST_HAILO_FRONTEND(object);
    GST_DEBUG_OBJECT(self, "dispose");

    gst_hailofrontend_reset(self);

    G_OBJECT_CLASS(gst_hailofrontend_parent_class)->dispose(object);
}

static void
gst_hailofrontend_reset(GstHailoFrontend *self)
{
    GST_DEBUG_OBJECT(self, "reset");
    if (self->sinkpad != NULL)
    {
        self->sinkpad = NULL;
    }

    // gst_hailofrontend_release_pad will be called automatically for each srcpad
    self->srcpads.clear();
}