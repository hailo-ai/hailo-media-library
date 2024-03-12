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

#include "gsthailofrontendbinsrc.hpp"
#include "multi_resize/gsthailomultiresize.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/isp_utils.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailofrontendbinsrc_debug_category);
#define GST_CAT_DEFAULT gst_hailofrontendbinsrc_debug_category

static void gst_hailofrontendbinsrc_set_property(GObject *object,
                                          guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailofrontendbinsrc_get_property(GObject *object,
                                          guint property_id, GValue *value, GParamSpec *pspec);                                          
static GstElement *gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc);
static GstElement *gst_hailofrontendbinsrc_init_capsfilter(GstHailoFrontendBinSrc *hailofrontendbinsrc);
static GstStateChangeReturn gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition);
static GstPad *gst_hailofrontendbinsrc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps);
static void gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailofrontendbinsrc_link_elements(GstElement *element);
static void gst_hailofrontendbinsrc_dispose(GObject *object);
static void gst_hailofrontendbinsrc_reset(GstHailoFrontendBinSrc *self);
static gboolean gst_hailofrontendbinsrc_denoise_enabled_changed(GstHailoFrontendBinSrc *self, bool enabled);
static gboolean gst_hailofrontendbinsrc_restart_v4l2src(GstHailoFrontendBinSrc *self);

enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_PRIVACY_MASK,
};

// Pad Templates
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src_%u",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_REQUEST,
                                                                   GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoFrontendBinSrc, gst_hailofrontendbinsrc, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailofrontendbinsrc_debug_category, "hailofrontendbinsrc", 0,
                                                "debug category for hailofrontendbinsrc element"));

static void
gst_hailofrontendbinsrc_class_init(GstHailoFrontendBinSrcClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    
    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class,
                                          "frontend vision pipeline source bin", "Hailo/Media-Library", "Frontend v4l2 source bin for vision pipelines.",
                                          "hailo.ai <contact@hailo.ai>");
    
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_dispose);

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
    
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_release_pad);
}

static void
gst_hailofrontendbinsrc_init(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    // Default values
    hailofrontendbinsrc->config_file_path = NULL;
    hailofrontendbinsrc->srcpads = {};
    hailofrontendbinsrc->m_elements_linked = FALSE;

    // Prepare internal elements
    // v4l2src
    hailofrontendbinsrc->m_v4l2src = gst_element_factory_make("v4l2src", NULL);
    if (nullptr == hailofrontendbinsrc->m_v4l2src) {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating v4l2src element in bin!"), (NULL));
    }
    g_object_set(hailofrontendbinsrc->m_v4l2src, "device", "/dev/video0", NULL);
    g_object_set(hailofrontendbinsrc->m_v4l2src, "io-mode", 2, NULL);

    // caps
    hailofrontendbinsrc->m_capsfilter = gst_hailofrontendbinsrc_init_capsfilter(hailofrontendbinsrc);

    // queue
    hailofrontendbinsrc->m_queue = gst_hailofrontendbinsrc_init_queue(hailofrontendbinsrc);

    // frontend
    hailofrontendbinsrc->m_frontend = gst_element_factory_make("hailofrontend", NULL);
    if (nullptr == hailofrontendbinsrc->m_frontend) {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating hailofrontend element in bin!"), (NULL));
    }
    MediaLibraryDenoise::callbacks_t callbacks;
    callbacks.on_enable_changed = [hailofrontendbinsrc](bool enabled)
    {
        // initialize caps negotiation to be passed downstream
        auto ret = gst_hailofrontendbinsrc_denoise_enabled_changed(hailofrontendbinsrc, enabled);
        if (!ret)
            GST_ERROR_OBJECT(hailofrontendbinsrc, "Failed to respond to low-light-enhancement settings change");
    };
    hailofrontendbinsrc->observe_denoising(callbacks);

    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailofrontendbinsrc),  hailofrontendbinsrc->m_v4l2src,
                                                    hailofrontendbinsrc->m_capsfilter,
                                                    hailofrontendbinsrc->m_queue,
                                                    hailofrontendbinsrc->m_frontend, NULL);
}

static GstElement *
gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    GstElement *queue = gst_element_factory_make("queue", NULL);
    if (nullptr == queue) {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
        return NULL;
    }
    // Passing 0 disables the features here
    // Note queue is leaky=2(downstream)
    g_object_set(queue, "leaky", (guint)2, NULL);
    g_object_set(queue, "max-size-time", (guint64)0, NULL);
    g_object_set(queue, "max-size-bytes", (guint)0, NULL);
    g_object_set(queue, "max-size-buffers", (guint)1, NULL);
    return queue;
}

static GstElement *
gst_hailofrontendbinsrc_init_capsfilter(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (nullptr == capsfilter) {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating capsfilter element in bin!"), (NULL));
        return NULL;
    }
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, 3840,
                                        "height", G_TYPE_INT, 2160,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);
    return capsfilter;
}

void gst_hailofrontendbinsrc_set_property(GObject *object, guint property_id,
                                   const GValue *value, GParamSpec *pspec)
{
    GstHailoFrontendBinSrc *hailofrontendbinsrc = GST_HAILO_FRONTEND_BINSRC(object);
    GST_DEBUG_OBJECT(hailofrontendbinsrc, "set_property");
    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        hailofrontendbinsrc->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(hailofrontendbinsrc, "config_file_path: %s", hailofrontendbinsrc->config_file_path);

        // set params for sub elements here
        g_object_set(hailofrontendbinsrc->m_frontend, "config-file-path", g_value_get_string(value), NULL);

        // Now that configuration is known, link the elements
        if (hailofrontendbinsrc->m_elements_linked == FALSE) 
        {
          gst_hailofrontendbinsrc_link_elements(GST_ELEMENT(hailofrontendbinsrc));
          hailofrontendbinsrc->m_elements_linked = TRUE;
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        hailofrontendbinsrc->config_string = g_strdup(g_value_get_string(value));
        GST_DEBUG_OBJECT(hailofrontendbinsrc, "config-string: %s", hailofrontendbinsrc->config_string.c_str());

        // set params for sub elements here
        g_object_set(hailofrontendbinsrc->m_frontend, "config-string", g_value_get_string(value), NULL);

        // Now that configuration is known, link the elements
        if (hailofrontendbinsrc->m_elements_linked == FALSE) 
        {
          gst_hailofrontendbinsrc_link_elements(GST_ELEMENT(hailofrontendbinsrc));
          hailofrontendbinsrc->m_elements_linked = TRUE;
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailofrontendbinsrc_get_property(GObject *object, guint property_id,
                                   GValue *value, GParamSpec *pspec)
{
    GstHailoFrontendBinSrc *hailofrontendbinsrc = GST_HAILO_FRONTEND_BINSRC(object);
    GST_DEBUG_OBJECT(hailofrontendbinsrc, "get_property");
    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, hailofrontendbinsrc->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailofrontendbinsrc->config_string.c_str());
        break;
    }
    case PROP_PRIVACY_MASK:
    {
        gpointer blender;
        g_object_get(hailofrontendbinsrc->m_frontend, "privacy-mask", &blender, NULL);
        g_value_set_pointer(value, blender);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstStateChangeReturn
gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    result = GST_ELEMENT_CLASS(gst_hailofrontendbinsrc_parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_PAUSED");
    }
    default:
        break;
    }

    return result;
}

static GstPad *
gst_hailofrontendbinsrc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps)
{
  GstPad *srcpad;
  GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
  GST_DEBUG_OBJECT(self, "FrontendBinSrc request new pad name: %s", name);

  // Get the source pad from GstHailoMultiResize that you want to expose
  GstPad *frontend_srcpad = gst_element_request_pad(self->m_frontend, templ, name, caps);
  GST_DEBUG_OBJECT(self, "FrontendBinSrc requested frontend_srcpad: %s", GST_PAD_NAME(frontend_srcpad));

  // Create a new ghost pad and target GstHailoMultiResize source pad
  srcpad = gst_ghost_pad_new_no_target(NULL, GST_PAD_SRC);
  gboolean link_status = gst_ghost_pad_set_target(GST_GHOST_PAD(srcpad), frontend_srcpad);
  GST_DEBUG_OBJECT(self, "FrontendBinSrc setting %s to target %s", GST_PAD_NAME(srcpad), GST_PAD_NAME(frontend_srcpad));
  if (!link_status) {
      GST_ERROR_OBJECT(self, "FrontendBinSrc failed to set %s to target %s", GST_PAD_NAME(srcpad), GST_PAD_NAME(frontend_srcpad));
  }

  // Unreference the frontend_srcpad when you're done with it
  gst_object_unref(frontend_srcpad);

  // Set the new ghostpad to active and add it to the bin
  gst_pad_set_active(srcpad, TRUE);
  gst_element_add_pad(element, srcpad);
  self->srcpads.emplace_back(srcpad);

  return srcpad;
}

static void
gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad)
{
  GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
  gchar *name = gst_pad_get_name(pad);
  GST_DEBUG_OBJECT(self, "Release pad: %s", name);

  GST_OBJECT_LOCK(self);

  // Find the corresponding source pad in GstHailoMultiResize
  GstPad *frontend_srcpad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));

  // Release the source pad in GstHailoFrontend
  gst_element_release_request_pad(self->m_frontend, frontend_srcpad);

  // Remove the ghost pad from GstHailoFrontendBinSrc
  gst_element_remove_pad(element, pad);

  GST_OBJECT_UNLOCK(self);

  // Unreference the frontend_srcpad when you're done with it
  gst_object_unref(frontend_srcpad);
}

static gboolean
gst_hailofrontendbinsrc_link_elements(GstElement *element)
{
  GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);

  // Link the elements
  gboolean link_status = gst_element_link_many(self->m_v4l2src,
                                               self->m_capsfilter,
                                               self->m_queue,
                                               self->m_frontend, NULL);

  if (!link_status) {
      GST_ERROR_OBJECT(self, "Failed to link elements in bin!");
      return FALSE;
  }

  return TRUE;
}

static void
gst_hailofrontendbinsrc_dispose(GObject *object)
{
  GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(object);
  GST_DEBUG_OBJECT(self, "dispose");

  // Restore the default ISP configurations before closing
  isp_utils::set_default_configuration();

  gst_hailofrontendbinsrc_reset(self);

  G_OBJECT_CLASS(gst_hailofrontendbinsrc_parent_class)->dispose(object);
}

static void
gst_hailofrontendbinsrc_reset(GstHailoFrontendBinSrc *self)
{
  GST_DEBUG_OBJECT(self, "reset");

  // gst_hailofrontendbinsrc_release_pad will be called automatically for each srcpad
  self->srcpads.clear();
}

static gboolean
gst_hailofrontendbinsrc_denoise_enabled_changed(GstHailoFrontendBinSrc *self, bool enabled)
{
  GST_DEBUG_OBJECT(self, "Denoise enabled changed to: %d", enabled);

  // Handle enabling/disabling denoise here
  if (enabled) {
    // Enabled ai low-light-enhancement, disable 3dnr
    isp_utils::set_denoise_configuration();
  } else {
    // Disabled ai low-light-enhancement, enable 3dnr
    isp_utils::set_default_configuration();
  }

  // restart v4l2src to apply the new 3a config
  gst_hailofrontendbinsrc_restart_v4l2src(self);

  return TRUE;
}

static gboolean
gst_hailofrontendbinsrc_restart_v4l2src(GstHailoFrontendBinSrc *self)
{
  GST_DEBUG_OBJECT(self, "Restarting v4l2src");

  // Wait until the state change is complete
  GstState state0;
  GstState pending_state0;
  gst_element_get_state(self->m_v4l2src, &state0, &pending_state0, GST_CLOCK_TIME_NONE);
  GST_DEBUG_OBJECT(self, "State before restart: %d, Pending state: %d", state0, pending_state0);

  // Stop the pipeline
  GstStateChangeReturn stop_status = gst_element_set_state(self->m_v4l2src, GST_STATE_NULL);
  if (stop_status == GST_STATE_CHANGE_FAILURE) {
    GST_DEBUG_OBJECT(self, "Failed to stop v4l2src");
    return FALSE;
  } else if (stop_status == GST_STATE_CHANGE_SUCCESS) {
    GST_DEBUG_OBJECT(self, "Stop status success");
  }

  // Wait until the state change is complete
  GstState state;
  GstState pending_state;
  GstStateChangeReturn ret = gst_element_get_state(self->m_v4l2src, &state, &pending_state, GST_CLOCK_TIME_NONE);
  if (ret == GST_STATE_CHANGE_SUCCESS) {
      GST_DEBUG_OBJECT(self, "Succeeded to stop v4l2src");
  }
  if (ret == GST_STATE_CHANGE_FAILURE) {
      GST_DEBUG_OBJECT(self, "Failed to stop v4l2src");
  }
  GST_DEBUG_OBJECT(self, "State after stop: %d, Pending state: %d", state, pending_state);

  // Start the pipeline
  if (state0 == GST_STATE_PLAYING) {
    GstStateChangeReturn start_status = gst_element_set_state(self->m_v4l2src, GST_STATE_PLAYING);
    if (start_status == GST_STATE_CHANGE_FAILURE) {
      GST_DEBUG_OBJECT(self, "Failed to start v4l2src");
      return FALSE;
    } else if (start_status == GST_STATE_CHANGE_SUCCESS) {
      GST_DEBUG_OBJECT(self, "Start status success");
    }
  }

  GstState state2;
  GstState pending_state2;
  gst_element_get_state(self->m_v4l2src, &state2, &pending_state2, GST_CLOCK_TIME_NONE);
  GST_DEBUG_OBJECT(self, "State after restart: %d, Pending state: %d", state2, pending_state2);

  return TRUE;
}