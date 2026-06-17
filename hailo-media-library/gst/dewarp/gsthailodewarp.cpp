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
#include "gsthailodewarp.hpp"

#include <tl/expected.hpp>
#include <gst/video/gstvideometa.h>
#include <stddef.h>
#include <stdexcept>

#include "buffer_utils/buffer_utils.hpp"
#include "buffer_pool.hpp"
#include "dewarp.hpp"
#include "dis_common.h"
#include "gstmedialibcommon.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailo_dewarp_debug);
#define GST_CAT_DEFAULT gst_hailo_dewarp_debug

// Pad Templates
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

#define _do_init GST_DEBUG_CATEGORY_INIT(gst_hailo_dewarp_debug, "hailodewarp", 0, "Hailo DIS and Dewarp element");

#define gst_hailo_dewarp_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoDewarp, gst_hailo_dewarp, GST_TYPE_ELEMENT, _do_init);

static GstFlowReturn gst_hailo_dewarp_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer);
static gboolean gst_hailo_dewarp_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static void gst_hailo_dewarp_dispose(GObject *object);
static void gst_hailo_dewarp_finalize(GObject *object);
static gboolean gst_hailo_dewarp_sink_event(GstPad *pad, GstObject *parent, GstEvent *gst_event);
static gboolean gst_hailo_dewarp_create(GstHailoDewarp *self);

enum
{
    PROP_PAD_0,
};

static void gst_hailo_dewarp_class_init(GstHailoDewarpClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class = (GObjectClass *)klass;
    gstelement_class = (GstElementClass *)klass;

    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_dewarp_dispose);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_dewarp_finalize);

    // Pad templates
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);
    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

    gst_element_class_set_static_metadata(gstelement_class, "Hailo DIS and Dewarp using dsp", "Hailo DIS and Dewarp",
                                          "Hailo DIS and Dewarp using dsp", "Hailo");
}

static void gst_hailo_dewarp_init(GstHailoDewarp *dewarp)
{
    GST_DEBUG_OBJECT(dewarp, "init");
    dewarp->params = new GstHailoDewarpParams();

    dewarp->params->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    dewarp->params->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(dewarp->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_dewarp_chain));
    gst_pad_set_query_function(dewarp->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_dewarp_sink_query));
    gst_pad_set_event_function(dewarp->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_dewarp_sink_event));

    GST_PAD_SET_PROXY_CAPS(dewarp->params->sinkpad.get());
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(dewarp), dewarp->params->sinkpad);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(dewarp), dewarp->params->srcpad);

    gst_hailo_dewarp_create(dewarp);
}

static GstFlowReturn gst_hailo_dewarp_push_output_frame(GstHailoDewarp *self, HailoMediaLibraryBufferPtr output_frame,
                                                        GstBuffer *buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;

    if (!output_frame || output_frame->buffer_data == nullptr)
    {
        GST_ERROR_OBJECT(self, "Trying to push null output frame");
        ret = GST_FLOW_ERROR;
        return ret;
    }

    if (GST_PAD_IS_FLUSHING(self->params->srcpad.get()))
    {
        GST_WARNING_OBJECT(self, "Srcpad %s is flushing, Not sending frame",
                           glib_cpp::get_name(self->params->srcpad).c_str());
        return ret;
    }

    // Get caps from srcpad
    GstCapsPtr caps = gst_pad_get_current_caps(self->params->srcpad);

    if (!caps)
    {
        GST_ERROR_OBJECT(self, "Failed to get caps from srcpad name %s",
                         glib_cpp::get_name(self->params->srcpad).c_str());
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Creating GstBuffer from dsp buffer");
    GstBufferPtr outbuf = gst_buffer_from_hailo_buffer(output_frame, caps);
    if (!outbuf)
    {
        GST_ERROR_OBJECT(self, "Failed to create GstBuffer from dsp buffer");
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", glib_cpp::get_name(self->params->srcpad).c_str());
    outbuf->pts = GST_BUFFER_PTS(buffer);
    outbuf->offset = GST_BUFFER_OFFSET(buffer);
    outbuf->duration = GST_BUFFER_DURATION(buffer);
    glib_cpp::ptrs::push_buffer_to_pad(self->params->srcpad, outbuf);

    return ret;
}

static GstFlowReturn gst_hailo_dewarp_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;
    GstBufferPtr buffer = gst_buffer;
    GstHailoDewarp *self = GST_HAILO_DEWARP(parent);

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    GstCapsPtr input_caps = gst_pad_get_current_caps(pad);

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }

    auto attached_profile = input_frame_ptr->get_attached_profile();
    if (!attached_profile->iq_settings.dewarp.enabled && !attached_profile->stabilizer_settings.dis.enabled &&
        !attached_profile->stabilizer_settings.eis.enabled && !attached_profile->stabilizer_settings.gyro.enabled)
    {
        GstCaps *input_caps = gst_pad_get_current_caps(pad);

        HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
        if (!input_frame_ptr)
        {
            GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
            return GST_FLOW_ERROR;
        }

        input_frame_ptr->optical_zoom_magnification =
            attached_profile->application_settings.optical_zoom.enabled
                ? attached_profile->application_settings.optical_zoom.magnification
                : 1.0f;

        GST_DEBUG_OBJECT(self, "Dewarp operations are disabled, pushing buffer to srcpad");
        ret = gst_hailo_dewarp_push_output_frame(self, input_frame_ptr, buffer);

        return ret;
    }

    HailoMediaLibraryBufferPtr output_frame_ptr = std::make_shared<hailo_media_library_buffer>();
    media_library_return media_lib_ret = self->params->medialib_dewarp->handle_frame(input_frame_ptr, output_frame_ptr);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Handle frame done");

    return gst_hailo_dewarp_push_output_frame(self, output_frame_ptr, buffer);
}

static gboolean gst_hailo_dewarp_sink_event(GstPad *pad, GstObject *parent, GstEvent *gst_event)
{
    GstEventPtr event = gst_event;
    GstHailoDewarp *self = GST_HAILO_DEWARP(parent);
    GST_DEBUG_OBJECT(self, "Received event from sinkpad");

    switch (GST_EVENT_TYPE(event))
    {
    default: {
        return glib_cpp::ptrs::pad_event_default(pad, parent, event);
    }
    }
}

static gboolean gst_hailo_dewarp_sink_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(parent);
    GST_DEBUG_OBJECT(self, "Received query from sinkpad");
    gboolean ret;

    switch (GST_QUERY_TYPE(query))
    {
    case GST_QUERY_ALLOCATION: {
        GST_DEBUG_OBJECT(self, "Received allocation query from sinkpad");
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        ret = gst_pad_query_default(pad, parent, query);
        break;
    }
    case GST_QUERY_ACCEPT_CAPS: {
        GstCapsPtr caps = glib_cpp::ptrs::parse_query_accept_caps(query);
        GST_DEBUG("accept caps %" GST_PTR_FORMAT, caps.get());
        gst_query_set_accept_caps_result(query, true);
        ret = TRUE;
        break;
    }
    default: {
        /* just call the default handler */
        ret = gst_pad_query_default(pad, parent, query);
        break;
    }
    }
    return ret;
}

static void gst_hailo_dewarp_finalize(GObject *object)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(object);
    GST_DEBUG_OBJECT(self, "finalize");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_hailo_dewarp_dispose(GObject *object)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(object);
    GST_DEBUG_OBJECT(self, "dispose");
    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static gboolean gst_hailo_dewarp_create(GstHailoDewarp *self)
{
    tl::expected<MediaLibraryDewarpPtr, media_library_return> dewarp = MediaLibraryDewarp::create();
    if (!dewarp.has_value())
    {
        GST_ERROR_OBJECT(self, "Dewarp configuration error: %d", dewarp.error());
        throw std::runtime_error("Dewarp failed to configure, check config file.");
    }
    self->params->medialib_dewarp = dewarp.value();
    return TRUE;
}
