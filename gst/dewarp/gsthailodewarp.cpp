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
#include "common/gstmedialibcommon.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "hailo_v4l2/hailo_v4l2.h"
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include <gst/video/video.h>
#include <tl/expected.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_hailo_dewarp_debug);
#define GST_CAT_DEFAULT gst_hailo_dewarp_debug

#define ROTATION_EVENT_NAME "HAILO_ROTATION_EVENT"
#define ROTATION_EVENT_PROP_NAME "rotation"

// Pad Templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS_ANY);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_dewarp_debug, "hailodewarp", 0, "Hailo DIS and Dewarp element");

#define gst_hailo_dewarp_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoDewarp, gst_hailo_dewarp, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_dewarp_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_dewarp_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailo_dewarp_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static void gst_hailo_dewarp_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailo_dewarp_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static GstStateChangeReturn gst_hailo_dewarp_change_state(GstElement *element, GstStateChange transition);
static void gst_hailo_dewarp_dispose(GObject *object);
static void gst_hailo_dewarp_finalize(GObject *object);
static void gst_hailo_dewarp_reset(GstHailoDewarp *self);
static void gst_hailo_dewarp_release_srcpad(GstPad *pad, GstHailoDewarp *self);

static gboolean gst_hailo_handle_caps_query(GstHailoDewarp *self, GstPad *pad, GstQuery *query);
static gboolean gst_hailo_dewarp_sink_event(GstPad *pad,
                                            GstObject *parent, GstEvent *event);
static gboolean gst_hailo_handle_caps_event(GstHailoDewarp *self, GstCaps *caps);
static gboolean gst_hailo_set_srcpad_caps(GstHailoDewarp *self, GstPad *srcpad, output_resolution_t &output_res);
static gboolean intersect_peer_srcpad_caps(GstHailoDewarp *self, GstPad *sinkpad, GstPad *srcpad, output_resolution_t &output_res);
static gboolean gst_hailo_dewarp_create(GstHailoDewarp *self);

enum
{
    PROP_PAD_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
};

static void
gst_hailo_dewarp_class_init(GstHailoDewarpClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class = (GObjectClass *)klass;
    gstelement_class = (GstElementClass *)klass;

    gobject_class->set_property = gst_hailo_dewarp_set_property;
    gobject_class->get_property = gst_hailo_dewarp_get_property;
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_dewarp_dispose);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_dewarp_finalize);

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
    // Pad templates
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);
    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

    gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_hailo_dewarp_change_state);

    gstelement_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailo_dewarp_release_pad);

    gst_element_class_set_static_metadata(gstelement_class, "Hailo DIS and Dewarp using dsp", "Hailo DIS and Dewarp",
                                          "Hailo DIS and Dewarp using dsp", "Hailo");
}

static void
gst_hailo_dewarp_init(GstHailoDewarp *dewarp)
{
    GST_DEBUG_OBJECT(dewarp, "init");
    dewarp->config_file_path = NULL;
    dewarp->medialib_dewarp = NULL;

    dewarp->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    dewarp->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(dewarp->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_dewarp_chain));
    gst_pad_set_query_function(dewarp->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_dewarp_sink_query));
    gst_pad_set_event_function(dewarp->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_dewarp_sink_event));

    GST_PAD_SET_PROXY_CAPS(dewarp->sinkpad);
    gst_element_add_pad(GST_ELEMENT(dewarp), dewarp->sinkpad);
    gst_element_add_pad(GST_ELEMENT(dewarp), dewarp->srcpad);
}

static GstFlowReturn gst_hailo_dewarp_push_output_frame(GstHailoDewarp *self,
                                                        hailo_media_library_buffer &output_frame,
                                                        GstBuffer *buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;

    if (output_frame.hailo_pix_buffer == nullptr)
    {
        GST_ERROR_OBJECT(self, "Trying to push null output frame");
        ret = GST_FLOW_ERROR;
        return ret;
    }

    HailoMediaLibraryBufferPtr hailo_buffer = std::make_shared<hailo_media_library_buffer>(std::move(output_frame));

    // Get caps from srcpad
    GstCaps *caps = gst_pad_get_current_caps(self->srcpad);

    if (!caps)
    {
        GST_ERROR_OBJECT(self, "Failed to get caps from srcpad name %s", gst_pad_get_name(self->srcpad));
        hailo_buffer->decrease_ref_count();
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Creating GstBuffer from dsp buffer");
    GstBuffer *gst_outbuf = gst_buffer_from_hailo_buffer(hailo_buffer, caps);
    gst_caps_unref(caps);
    if (!gst_outbuf)
    {
        GST_ERROR_OBJECT(self, "Failed to create GstBuffer from dsp buffer");
        hailo_buffer->decrease_ref_count();
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", gst_pad_get_name(self->srcpad));
    gst_outbuf->pts = GST_BUFFER_PTS(buffer);
    gst_outbuf->offset = GST_BUFFER_OFFSET(buffer);
    gst_outbuf->duration = GST_BUFFER_DURATION(buffer);
    gst_pad_push(self->srcpad, gst_outbuf);

    return ret;
}

static GstFlowReturn gst_hailo_dewarp_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(parent);
    GstFlowReturn ret = GST_FLOW_OK;
    GstHailoV4l2Meta *meta = nullptr;

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    // If Dewarp disbled, just push the buffer to srcpad
    if (!self->medialib_dewarp->get_ldc_configs().dewarp_config.enabled)
    {
        GST_DEBUG_OBJECT(self, "Dewarp disabled, pushing buffer to srcpad");
        gst_pad_push(self->srcpad, buffer);
        return ret;
    }

    // If DIS is enabled
    if (self->medialib_dewarp->get_ldc_configs().dis_config.enabled)
    {
        // Get the vsm from the buffer
        meta = reinterpret_cast<GstHailoV4l2Meta *>(gst_buffer_get_meta(buffer, g_type_from_name(HAILO_V4L2_META_API_NAME)));
        if (meta == nullptr)
        {
            GST_ERROR_OBJECT(self, "Cannot get hailo v4l2 metadata from buffer, check that source provides VSM (V4L2) or disable DIS");
            return GST_FLOW_ERROR;
        }
        else
        {
            GST_DEBUG_OBJECT(self, "Got VSM metadata, index: %d vsm x: %d vsm y: %d current fps: %d", meta->v4l2_index, meta->vsm.dx, meta->vsm.dy, meta->isp_ae_fps);
        }
    }

    GstCaps *input_caps = gst_pad_get_current_caps(pad);

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }
    gst_caps_unref(input_caps);

    hailo_media_library_buffer output_frame;

    GST_DEBUG_OBJECT(self, "Call media library handle frame - GstBuffer offset %ld", GST_BUFFER_OFFSET(buffer));
    media_library_return media_lib_ret = self->medialib_dewarp->handle_frame(*input_frame_ptr.get(), output_frame);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Handle frame done");
    ret = gst_hailo_dewarp_push_output_frame(self, output_frame, buffer);
    gst_buffer_unref(buffer);

    return ret;
}

static GstCaps *
gst_hailo_create_caps_from_output_config(GstHailoDewarp *self, output_resolution_t &output_res)
{
    GstCaps *caps;
    guint framerate = (guint)output_res.framerate;
    if (framerate == 0)
        framerate = 1;

    // Format does not change from input to output
    input_video_config_t &input_config = self->medialib_dewarp->get_input_video_config();
    dsp_image_format_t &dsp_image_format = input_config.format;
    std::string format = "";
    switch (dsp_image_format)
    {
    case DSP_IMAGE_FORMAT_RGB:
        format = "RGB";
        break;
    case DSP_IMAGE_FORMAT_GRAY8:
        format = "GRAY8";
        break;
    case DSP_IMAGE_FORMAT_NV12:
        format = "NV12";
        break;
    case DSP_IMAGE_FORMAT_A420:
        format = "A420";
        break;
    default:
        GST_ERROR_OBJECT(self, "Unsupported dsp image format %d", dsp_image_format);
        return NULL;
    }

    GST_DEBUG_OBJECT(self, "Creating caps - width = %ld height = %ld framerate = %d", output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.framerate);
    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING, format.c_str(),
                               "width", G_TYPE_INT, (guint)output_res.dimensions.destination_width,
                               "height", G_TYPE_INT, (guint)output_res.dimensions.destination_height,
                               "framerate", GST_TYPE_FRACTION, framerate, 1,
                               NULL);

    return caps;
}

static gboolean
gst_hailo_set_srcpad_caps(GstHailoDewarp *self, GstPad *srcpad, output_resolution_t &output_res)
{
    GstCaps *caps_result, *outcaps, *query_caps = NULL;
    gboolean ret = TRUE;

    query_caps = gst_hailo_create_caps_from_output_config(self, output_res);

    // Query the peer srcpad to obtain wanted resolution
    caps_result = gst_pad_peer_query_caps(srcpad, query_caps);

    // gst_caps_fixate takes ownership of caps_result, no need to unref it.
    outcaps = gst_caps_fixate(caps_result);

    // Check if outcaps intersects of query_caps
    GST_DEBUG_OBJECT(self, "Caps event - fixated peer srcpad caps %" GST_PTR_FORMAT, outcaps);

    if (gst_caps_is_empty(outcaps) || !gst_caps_is_fixed(outcaps))
    {
        GST_ERROR_OBJECT(self, "Caps event - set caps is not possible, Failed to match required caps with srcpad %s", gst_pad_get_name(srcpad));
        ret = FALSE;
    }
    else
    {
        // set the caps on the peer srcpad
        gboolean srcpad_set_caps_result = gst_pad_set_caps(srcpad, outcaps);

        if (!srcpad_set_caps_result)
        {
            GST_ERROR_OBJECT(self, "Failed to set caps on srcpad %s", gst_pad_get_name(srcpad));
            ret = FALSE;
        }
    }

    gst_caps_unref(query_caps);
    gst_caps_unref(outcaps);
    return ret;
}

static tl::expected<dsp_image_format_t, media_library_return>
gstchar_format_to_dsp_format(const gchar *format)
{
    if (strcmp(format, "RGB") == 0)
        return DSP_IMAGE_FORMAT_RGB;
    else if (strcmp(format, "GRAY8") == 0)
        return DSP_IMAGE_FORMAT_GRAY8;
    else if (strcmp(format, "NV12") == 0)
        return DSP_IMAGE_FORMAT_NV12;
    else if (strcmp(format, "A420") == 0)
        return DSP_IMAGE_FORMAT_A420;
    else
        return tl::make_unexpected(MEDIA_LIBRARY_CONFIGURATION_ERROR);
}

static gboolean
gst_hailo_handle_caps_event(GstHailoDewarp *self, GstCaps *caps)
{
    gboolean ret = TRUE;
    if (self->medialib_dewarp == nullptr)
    {
        GST_ERROR_OBJECT(self, "self->medialib_dewarp nullptr at time of caps query");
        return FALSE;
    }

    // Set the input resolution by caps
    GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint width, height, numerator, denominator;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    gst_structure_get_fraction(structure, "framerate", &numerator, &denominator);
    tl::expected<dsp_image_format_t, media_library_return> format = gstchar_format_to_dsp_format(gst_structure_get_string(structure, "format"));
    if (!format.has_value())
    {
        GST_ERROR_OBJECT(self, "Failed to convert format %s to dsp format", gst_structure_get_string(structure, "format"));
        return FALSE;
    }

    media_library_return config_status = self->medialib_dewarp->set_input_video_config(width, height, numerator / denominator, format.value());
    if (config_status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library Dewarp could not accept sink caps, failed on error %d", config_status);
        ret = FALSE;
    }

    output_resolution_t &output_config = self->medialib_dewarp->get_output_video_config();

    ret = gst_hailo_set_srcpad_caps(self, self->srcpad, output_config);
    if (!ret)
        return FALSE;

    return ret;
}

static gboolean
gst_hailo_dewarp_sink_event(GstPad *pad,
                            GstObject *parent, GstEvent *event)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(parent);
    gboolean ret = FALSE;
    GST_DEBUG_OBJECT(self, "Received event from sinkpad");

    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_CAPS:
    {
        GST_DEBUG_OBJECT(self, "Received caps event from sinkpad");
        GstCaps *caps;
        gst_event_parse_caps(event, &caps);
        ret = gst_hailo_handle_caps_event(self, caps);
        gst_event_unref(event);
        break;
    }
    default:
    {
        /* just call the default handler */
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }
    }
    return ret;
}

static gboolean
intersect_peer_srcpad_caps(GstHailoDewarp *self, GstPad *sinkpad, GstPad *srcpad, output_resolution_t &output_res)
{
    GstCaps *query_caps, *intersect_caps, *peercaps;
    gboolean ret = TRUE;

    query_caps = gst_hailo_create_caps_from_output_config(self, output_res);

    /* query the peer with the transformed filter */
    peercaps = gst_pad_peer_query_caps(srcpad, query_caps);
    GST_DEBUG_OBJECT(sinkpad, "peercaps %" GST_PTR_FORMAT, peercaps);

    /* intersect with the peer caps */
    intersect_caps = gst_caps_intersect(query_caps, peercaps);

    GST_DEBUG_OBJECT(sinkpad, "intersect_caps %" GST_PTR_FORMAT, intersect_caps);
    // validate intersect caps
    if (gst_caps_is_empty(intersect_caps))
    {
        GST_ERROR_OBJECT(self, "Failed to intersect caps - with srcpad %s and requested width %ld height %ld and framerate %d", gst_pad_get_name(srcpad), output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.framerate);
        ret = FALSE;
    }

    if (peercaps)
        gst_caps_unref(peercaps);

    gst_caps_unref(intersect_caps);
    gst_caps_unref(query_caps);
    return ret;
}

static gboolean
gst_hailo_handle_caps_query(GstHailoDewarp *self, GstPad *pad, GstQuery *query)
{
    // get pad name and direction
    GstPadDirection pad_direction = gst_pad_get_direction(pad);

    gchar *pad_name = gst_pad_get_name(pad);
    GST_DEBUG_OBJECT(pad, "Received caps query from sinkpad name %s direction %d", pad_name, pad_direction);
    g_free(pad_name);
    GstCaps *caps_result, *allowed_caps, *qcaps;
    /* we should report the supported caps here which are all */
    allowed_caps = gst_pad_get_pad_template_caps(pad);

    gst_query_parse_caps(query, &qcaps);
    if (qcaps && allowed_caps && !gst_caps_is_any(allowed_caps))
    {
        GST_DEBUG_OBJECT(pad, "qcaps %" GST_PTR_FORMAT, qcaps);
        // caps query - intersect template caps (allowed caps) with incomming caps query
        caps_result = gst_caps_intersect(allowed_caps, qcaps);
    }
    else
    {
        // no caps query - return template caps
        caps_result = allowed_caps;
    }

    GST_DEBUG_OBJECT(pad, "allowed template  %" GST_PTR_FORMAT, caps_result);
    if (self->medialib_dewarp == nullptr)
    {
        GST_ERROR_OBJECT(pad, "self->medialib_dewarp nullptr at time of caps query");
        return FALSE;
    }
    output_resolution_t &output_config = self->medialib_dewarp->get_output_video_config();
    if (!intersect_peer_srcpad_caps(self, pad, self->srcpad, output_config))
    {
        gst_caps_unref(caps_result);
        return FALSE;
    }

    // set the caps result
    gst_query_set_caps_result(query, caps_result);
    gst_caps_unref(caps_result);

    return TRUE;
}

static gboolean
gst_hailo_dewarp_sink_query(GstPad *pad,
                            GstObject *parent, GstQuery *query)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(parent);
    GST_DEBUG_OBJECT(self, "Received query from sinkpad");
    gboolean ret;

    switch (GST_QUERY_TYPE(query))
    {
    case GST_QUERY_ALLOCATION:
    {
        GST_DEBUG_OBJECT(self, "Received allocation query from sinkpad");
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        ret = gst_pad_query_default(pad, parent, query);
        break;
    }
    case GST_QUERY_CAPS:
    {
        ret = gst_hailo_handle_caps_query(self, pad, query);
        break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
        GstCaps *caps;
        gst_query_parse_accept_caps(query, &caps);
        GST_DEBUG("accept caps %" GST_PTR_FORMAT, caps);
        gst_query_set_accept_caps_result(query, true);
        ret = TRUE;
        break;
    }
    default:
    {
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
    if (self->config_file_path)
    {
        g_free(self->config_file_path);
        self->config_file_path = NULL;
    }

    if (self->medialib_dewarp)
    {
        self->medialib_dewarp.reset();
        self->medialib_dewarp = NULL;
    }
}

static void gst_hailo_dewarp_dispose(GObject *object)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(object);
    GST_DEBUG_OBJECT(self, "dispose");

    gst_hailo_dewarp_reset(self);

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
gst_hailo_dewarp_release_srcpad(GstPad *pad, GstHailoDewarp *self)
{
    if (pad != NULL)
    {
        GST_DEBUG_OBJECT(self, "Releasing srcpad %s", gst_pad_get_name(pad));
        gst_pad_set_active(pad, FALSE);
        gst_element_remove_pad(GST_ELEMENT_CAST(self), pad);
    }
}

static void
gst_hailo_dewarp_reset(GstHailoDewarp *self)
{
    GST_DEBUG_OBJECT(self, "reset");
    if (self->sinkpad != NULL)
    {
        self->sinkpad = NULL;
    }

    gst_hailo_dewarp_release_srcpad(self->srcpad, self);
}

static void gst_hailo_dewarp_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(object);

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        self->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "config_file_path: %s", self->config_file_path);
        self->config_string = gstmedialibcommon::read_json_string_from_file(self->config_file_path);
        if (self->medialib_dewarp == nullptr)
        {
            gst_hailo_dewarp_create(self);
        }
        else
        {
            bool enabled_ops = self->medialib_dewarp->check_ops_enabled_from_config_string(self->config_string);

            if (!enabled_ops)
            {
                break;
            }

            media_library_return config_status = self->medialib_dewarp->configure(self->config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        self->config_string = std::string(g_value_get_string(value));
        gstmedialibcommon::strip_string_syntax(self->config_string);

        if (self->medialib_dewarp == nullptr)
        {
            gst_hailo_dewarp_create(self);
        }
        else
        {
            bool enabled_ops = self->medialib_dewarp->check_ops_enabled_from_config_string(self->config_string);

            if (!enabled_ops) 
            {
                break;
            }
            media_library_return config_status = self->medialib_dewarp->configure(self->config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void
gst_hailo_dewarp_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(object);

    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, self->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, self->config_string.c_str());
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean
gst_hailo_dewarp_create(GstHailoDewarp *self)
{
    tl::expected<MediaLibraryDewarpPtr, media_library_return> dewarp = MediaLibraryDewarp::create(self->config_string);
    if (!dewarp.has_value())
    {
        GST_ERROR_OBJECT(self, "Dewarp configuration error: %d", dewarp.error());
        throw std::runtime_error("Dewarp failed to configure, check config file.");
    }
    self->medialib_dewarp = dewarp.value();

    // set event callbacks
    MediaLibraryDewarp::callbacks_t callbacks;
    callbacks.on_output_resolution_change = [self](output_resolution_t &output_res)
    {
        // initialize caps negotiation to be passed downstream
        auto ret = gst_hailo_set_srcpad_caps(self, self->srcpad, output_res);
        if (!ret)
            GST_ERROR_OBJECT(self, "Failed to set srcpad caps after output resolution change callback was called");
    };
    callbacks.on_rotation_change = [self](rotation_angle_t &rotation)
    {
        // create a custom gstreamer event that notifies the rotation change
        GstStructure *structure = gst_structure_new(ROTATION_EVENT_NAME, ROTATION_EVENT_PROP_NAME, G_TYPE_UINT, (guint)rotation, NULL);
        auto ret = gst_pad_push_event(self->srcpad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure));

        if (!ret)
            GST_ERROR_OBJECT(self, "Failed to push rotation event to srcpad after rotation change callback was called");
    };
    self->medialib_dewarp->observe(callbacks);

    return TRUE;
}

static void
gst_hailo_dewarp_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoDewarp *self = GST_HAILO_DEWARP(element);
    gchar *name = gst_pad_get_name(pad);
    GST_DEBUG_OBJECT(self, "Release pad: %s", name);
    gst_element_remove_pad(element, pad);
}

static GstStateChangeReturn gst_hailo_dewarp_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
    GstHailoDewarp *self = GST_HAILO_DEWARP(element);
    result = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

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