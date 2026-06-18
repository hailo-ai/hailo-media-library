/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "gsthailoconfigattacher.hpp"

#include <glib-object.h>
#include <tl/expected.hpp>
#include <gst/gstparamspecs.h>
#include <gst/video/gstvideometa.h>
#include <stddef.h>

#include "buffer_utils.hpp"
#include "common/gstmedialibcommon.hpp"
#include "media_library/config_manager.hpp"
#include "gstmedialibptrs.hpp"
#include "buffer_pool.hpp"
#include "config_attacher.hpp"
#include "media_library_types.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailo_config_attacher_debug);
#define GST_CAT_DEFAULT gst_hailo_config_attacher_debug

// Pad Templates
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

#define _do_init                                                                                                       \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_config_attacher_debug, "hailoconfigattacher", 0, "Hailo config attacher element");

#define gst_hailo_config_attacher_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoConfigAttacher, gst_hailo_config_attacher, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_config_attacher_finalize(GObject *object);
static void gst_hailo_config_attacher_set_property(GObject *object, guint property_id, const GValue *value,
                                                   GParamSpec *pspec);
static void gst_hailo_config_attacher_get_property(GObject *object, guint property_id, GValue *, GParamSpec *);
static GstFlowReturn gst_hailo_config_attacher_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer);
static gboolean gst_hailo_config_attacher_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);

enum
{
    PROP_PAD_0,
    PROP_CONFIG_MANAGER_INTERACTOR,
    PROP_CONFIG_MANAGER_OWNER,
};

static void gst_hailo_config_attacher_class_init(GstHailoConfigAttacherClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    // Pad templates
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    // Set metadata
    gst_element_class_set_static_metadata(element_class, "config attacher element", "Hailo/Media-Library",
                                          "Config attacher element for hailo media library pipeline",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailo_config_attacher_set_property;
    gobject_class->get_property = gst_hailo_config_attacher_get_property;
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_config_attacher_finalize);

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_MANAGER_INTERACTOR,
        g_param_spec_pointer("config-manager-interactor", "Config Manager Interactor pointer",
                             "Pointer to Config Manager Interactor",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_MANAGER_OWNER,
        g_param_spec_boolean("config-manager-owner", "Config Manager Owner",
                             "Should construct Config Manager Interactor", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
}

static void gst_hailo_config_attacher_init(GstHailoConfigAttacher *config_attacher)
{
    GST_DEBUG_OBJECT(config_attacher, "init");
    config_attacher->params = new GstHailoConfigAttacherParams();

    config_attacher->params->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    config_attacher->params->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(config_attacher->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_config_attacher_chain));
    gst_pad_set_query_function(config_attacher->params->sinkpad,
                               GST_DEBUG_FUNCPTR(gst_hailo_config_attacher_sink_query));

    GST_PAD_SET_PROXY_CAPS(config_attacher->params->sinkpad.get());
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(config_attacher), config_attacher->params->sinkpad);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(config_attacher), config_attacher->params->srcpad);
}

static void gst_hailo_config_attacher_get_property(GObject *object, guint property_id, GValue *, GParamSpec *)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, nullptr);
}

static void gst_hailo_config_attacher_set_property(GObject *object, guint property_id, const GValue *value,
                                                   GParamSpec *)
{
    GstHailoConfigAttacher *self = GST_HAILO_CONFIG_ATTACHER(object);

    switch (property_id)
    {
    case PROP_CONFIG_MANAGER_INTERACTOR: {
        self->params->config_manager_interactor = static_cast<ConfigManagerInteractor *>(g_value_get_pointer(value));
        self->params->config_attacher = std::make_unique<ConfigAttacher>(self->params->config_manager_interactor);
        break;
    }
        // this property should be used only in cases where we want to use the hailo encoder without frontend
    case PROP_CONFIG_MANAGER_OWNER: {
        frontend_config_t frontend_dummy_config;
        self->params->config_manager_interactor =
            ConfigManagerInteractor::create_dummy_profile_interactor(frontend_dummy_config).value().release();
        gst_mode_config_manager_interactor = self->params->config_manager_interactor;
        self->params->m_is_config_manager_owner = true;
        self->params->config_attacher = std::make_unique<ConfigAttacher>(self->params->config_manager_interactor);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, nullptr);
        break;
    }
}

static GstFlowReturn gst_hailo_config_attacher_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer)
{
    GstHailoConfigAttacher *self = GST_HAILO_CONFIG_ATTACHER(parent);
    GstBufferPtr buffer = gst_buffer;
    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    GstCapsPtr input_caps = gst_pad_get_current_caps(pad);

    if (self->params->config_manager_interactor == nullptr)
    {
        GST_ERROR_OBJECT(self, "Config Manager Interactor is not set");
        return GST_FLOW_ERROR;
    }

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }

    if (!self->params->config_attacher->attach_config(input_frame_ptr))
    {
        GST_ERROR_OBJECT(self, "Failed to attach config to frame");
        return GST_FLOW_ERROR;
    }

    GstBufferPtr gst_outbuf = gst_buffer_from_hailo_buffer(input_frame_ptr, input_caps);
    gst_outbuf->pts = GST_BUFFER_PTS(buffer);
    gst_outbuf->offset = GST_BUFFER_OFFSET(buffer);
    gst_outbuf->duration = GST_BUFFER_DURATION(buffer);

    glib_cpp::ptrs::push_buffer_to_pad(self->params->srcpad, gst_outbuf);

    return GST_FLOW_OK;
}

static void gst_hailo_config_attacher_finalize(GObject *object)
{
    GstHailoConfigAttacher *self = GST_HAILO_CONFIG_ATTACHER(object);
    GST_DEBUG_OBJECT(self, "finalize");
    if (self->params != nullptr)
    {
        if (self->params->m_is_config_manager_owner)
        {
            delete self->params->config_manager_interactor;
            self->params->config_manager_interactor = nullptr;
            gst_mode_config_manager_interactor = nullptr;
        }
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_hailo_config_attacher_sink_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
    GstHailoConfigAttacher *self = GST_HAILO_CONFIG_ATTACHER(parent);
    GST_DEBUG_OBJECT(self, "Received query from sinkpad");

    if (GST_QUERY_TYPE(query) == GST_QUERY_ALLOCATION)
    {
        GST_DEBUG_OBJECT(self, "Received allocation query from sinkpad");
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
    }

    return gst_pad_query_default(pad, parent, query);
}
