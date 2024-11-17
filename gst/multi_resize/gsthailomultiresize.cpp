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
#include "gsthailomultiresize.hpp"
#include "common/gstmedialibcommon.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "media_library/privacy_mask.hpp"
#include <gst/video/video.h>
#include <tl/expected.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_hailo_multi_resize_debug);
#define GST_CAT_DEFAULT gst_hailo_multi_resize_debug

#define ROTATION_EVENT_NAME "HAILO_ROTATION_EVENT"
#define ROTATION_EVENT_PROP_NAME "rotation"

// Pad Templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src_%u",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_REQUEST,
                                                                   GST_STATIC_CAPS_ANY);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_multi_resize_debug, "hailomultiresize", 0, "Hailo Multi Resize element");

#define gst_hailo_multi_resize_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoMultiResize, gst_hailo_multi_resize, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_multi_resize_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_multi_resize_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailo_multi_resize_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstPad *gst_hailo_multi_resize_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps);
static void gst_hailo_multi_resize_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailo_multi_resize_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static void gst_hailo_multi_resize_dispose(GObject *object);
static void gst_hailo_multi_resize_finalize(GObject *object);
static void gst_hailo_multi_resize_reset(GstHailoMultiResize *self);
static void gst_hailo_multi_resize_release_srcpad(GstPad *pad, GstHailoMultiResize *self);

static gboolean gst_hailo_handle_caps_query(GstHailoMultiResize *self, GstPad *pad, GstQuery *query);
static gboolean gst_hailo_multi_resize_sink_event(GstPad *pad,
                                                  GstObject *parent, GstEvent *event);
static gboolean gst_hailo_handle_caps_event(GstHailoMultiResize *self, GstCaps *caps);
static gboolean gst_hailo_set_srcpad_caps(GstHailoMultiResize *self, GstPad *srcpad, output_resolution_t &output_res);
static gboolean intersect_peer_srcpad_caps(GstHailoMultiResize *self, GstPad *sinkpad, GstPad *srcpad, output_resolution_t &output_res);
static gboolean gst_hailo_multi_resize_create(GstHailoMultiResize *self, std::string config_string);
static gboolean gst_hailo_multi_resize_on_output_caps_changed(GstHailoMultiResize *self, std::vector<output_resolution_t> &outputs_res);

enum
{
    PROP_PAD_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_PRIVACY_MASK,
    PROP_CONFIG,
};

static void
gst_hailo_multi_resize_class_init(GstHailoMultiResizeClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class = (GObjectClass *)klass;
    gstelement_class = (GstElementClass *)klass;

    gobject_class->set_property = gst_hailo_multi_resize_set_property;
    gobject_class->get_property = gst_hailo_multi_resize_get_property;
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_dispose);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_finalize);

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

    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_pointer("config", "multi resize config", "Multi Resize config as multi_resize_config_t",
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    // Pad templates
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);
    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

    gstelement_class->request_new_pad =
        GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_request_new_pad);
    gstelement_class->release_pad =
        GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_release_pad);

    gst_element_class_set_static_metadata(gstelement_class, "1 to N multiple resize using dsp", "Hailo Multi Resize",
                                          "1 to N multiple resize using dsp", "Hailo");
}

static void
gst_hailo_multi_resize_init(GstHailoMultiResize *multi_resize)
{
    GST_DEBUG_OBJECT(multi_resize, "init");
    multi_resize->config_file_path = NULL;
    multi_resize->srcpads = std::make_shared<std::vector<GstPad *>>();
    multi_resize->medialib_multi_resize = NULL;

    multi_resize->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");

    gst_pad_set_chain_function(multi_resize->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_chain));
    gst_pad_set_query_function(multi_resize->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_sink_query));
    gst_pad_set_event_function(multi_resize->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_sink_event));

    GST_PAD_SET_PROXY_CAPS(multi_resize->sinkpad);
    gst_element_add_pad(GST_ELEMENT(multi_resize), multi_resize->sinkpad);
}

static GstFlowReturn gst_hailo_multi_resize_push_output_frames(GstHailoMultiResize *self,
                                                               std::vector<HailoMediaLibraryBufferPtr> &output_frames,
                                                               GstBuffer *buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;
    guint output_frames_size = output_frames.size();
    if (output_frames_size < self->srcpads->size())
    {
        GST_ERROR_OBJECT(self, "Number of output frames (%d) is lower than the number of srcpads (%ld)", output_frames_size, self->srcpads->size());
        return GST_FLOW_ERROR;
    }
    else if (output_frames_size > self->srcpads->size())
    {
        GST_WARNING_OBJECT(self, "Number of output frames (%d) is higher than the number of srcpads (%ld)", output_frames_size, self->srcpads->size());
    }

    output_video_config_t output_conf = self->medialib_multi_resize->get_output_video_config();

    for (guint i = 0; i < self->srcpads->size(); i++)
    {
        gchar *srcpad_name = gst_pad_get_name(self->srcpads->at(i));
        if (output_frames[i]->buffer_data == nullptr)
        {
            GST_DEBUG_OBJECT(self, "Skipping output frame %d to match requested framerate", i);
            g_free(srcpad_name);
            continue;
        }

        HailoMediaLibraryBufferPtr hailo_buffer = output_frames[i];
        GstPad *srcpad = self->srcpads->at(i);
        if (GST_PAD_IS_FLUSHING(srcpad))
        {
            GST_WARNING_OBJECT(self, "srcpad %s is flushing", srcpad_name);
            g_free(srcpad_name);
            continue;
        }
        // Get caps from srcpad
        GstCaps *caps = gst_pad_get_current_caps(srcpad);

        if (!caps)
        {
            GST_ERROR_OBJECT(self, "Failed to get caps from srcpad name %s", srcpad_name);
            g_free(srcpad_name);
            ret = GST_FLOW_ERROR;
            continue;
        }

        GST_DEBUG_OBJECT(self, "Creating GstBuffer from dsp buffer");
        GstBuffer *gst_outbuf = gst_buffer_from_hailo_buffer(hailo_buffer, caps);
        gst_caps_unref(caps);
        if (!gst_outbuf)
        {
            GST_ERROR_OBJECT(self, "Failed to create GstBuffer from dsp buffer");
            g_free(srcpad_name);
            ret = GST_FLOW_ERROR;
            continue;
        }

        GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", srcpad_name);
        g_free(srcpad_name);
        gst_outbuf->pts = GST_BUFFER_PTS(buffer);
        gst_outbuf->offset = GST_BUFFER_OFFSET(buffer);
        // Duration changes according to the requested output framerate
        gst_outbuf->duration = GST_BUFFER_DURATION(buffer) * (hailo_buffer->isp_ae_fps / output_conf.resolutions[i].framerate);
        gst_pad_push(srcpad, gst_outbuf);
    }

    return ret;
}

static GstFlowReturn gst_hailo_multi_resize_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(parent);
    GstFlowReturn ret = GST_FLOW_OK;

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    GstCaps *input_caps = gst_pad_get_current_caps(pad);

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }
    gst_caps_unref(input_caps);

    std::vector<HailoMediaLibraryBufferPtr> output_frames;

    GST_DEBUG_OBJECT(self, "Call media library handle frame - GstBuffer offset %ld", GST_BUFFER_OFFSET(buffer));
    media_library_return media_lib_ret = self->medialib_multi_resize->handle_frame(input_frame_ptr, output_frames);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Handle frame done");
    ret = gst_hailo_multi_resize_push_output_frames(self, output_frames, buffer);
    gst_buffer_unref(buffer);

    return ret;
}

static GstCaps *
gst_hailo_create_caps_from_output_config(GstHailoMultiResize *self, output_resolution_t &output_res)
{
    GstCaps *caps;
    guint framerate = (guint)output_res.framerate;
    // TODO (MSW-4090): support 0 fps --> disable stream
    if (framerate == 0)
        framerate = 1;

    output_video_config_t &output_config = self->medialib_multi_resize->get_output_video_config();
    HailoFormat &hailo_format = output_config.format;
    std::string format = "";
    switch (hailo_format)
    {
    case HAILO_FORMAT_RGB:
        format = "RGB";
        break;
    case HAILO_FORMAT_GRAY8:
        format = "GRAY8";
        break;
    case HAILO_FORMAT_NV12:
        format = "NV12";
        break;
    case HAILO_FORMAT_A420:
        format = "A420";
        break;
    default:
        GST_ERROR_OBJECT(self, "Unsupported dsp image format %d", hailo_format);
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

static gboolean gst_hailo_set_srcpad_caps(GstHailoMultiResize *self, GstPad *srcpad, output_resolution_t &output_res)
{
    GstCaps *caps_result, *outcaps, *query_caps = NULL;
    gboolean ret = TRUE;
    gchar *srcpad_name = gst_pad_get_name(srcpad);

    query_caps = gst_hailo_create_caps_from_output_config(self, output_res);

    // Query the peer srcpad to obtain wanted resolution
    caps_result = gst_pad_peer_query_caps(srcpad, query_caps);

    // gst_caps_fixate takes ownership of caps_result, no need to unref it.
    outcaps = gst_caps_fixate(caps_result);

    // Check if outcaps intersects of query_caps
    GST_DEBUG_OBJECT(self, "Caps event - fixated peer srcpad caps %" GST_PTR_FORMAT, outcaps);

    if (gst_caps_is_empty(outcaps) || !gst_caps_is_fixed(outcaps))
    {
        GST_ERROR_OBJECT(self, "Caps event - set caps is not possible, Failed to match required caps with srcpad %s", srcpad_name);
        ret = FALSE;
    }
    else
    {
        // set the caps on the peer srcpad
        gboolean srcpad_set_caps_result = gst_pad_set_caps(srcpad, outcaps);
        if (!srcpad_set_caps_result)
        {
            GST_ERROR_OBJECT(self, "Failed to set caps on srcpad %s", srcpad_name);
            ret = FALSE;
        }
    }

    g_free(srcpad_name);
    gst_caps_unref(query_caps);
    gst_caps_unref(outcaps);
    return ret;
}

static gboolean gst_hailo_multi_resize_on_output_caps_changed(GstHailoMultiResize *self, std::vector<output_resolution_t> &outputs_res)
{
    guint num_of_srcpads = self->srcpads->size();

    if (num_of_srcpads > outputs_res.size())
    {
        GST_ERROR_OBJECT(self, "Number of srcpads (%d) exceeds number of output resolutions (%ld)", num_of_srcpads, outputs_res.size());
        return FALSE;
    }

    for (guint i = 0; i < num_of_srcpads; i++)
    {
        if (!gst_hailo_set_srcpad_caps(self, self->srcpads->at(i), outputs_res[i]))
            return FALSE;
    }

    return TRUE;
}

static gboolean gst_hailo_handle_caps_event(GstHailoMultiResize *self, GstCaps *caps)
{
    gboolean ret = TRUE;
    if (self->medialib_multi_resize == nullptr)
    {
        GST_ERROR_OBJECT(self, "self->medialib_multi_resize nullptr at time of caps query");
        return FALSE;
    }

    std::vector<output_resolution_t> &outputs = self->medialib_multi_resize->get_output_video_config().resolutions;
    ret = gst_hailo_multi_resize_on_output_caps_changed(self, outputs);
    if (!ret)
        return FALSE;

    // Set the input resolution by caps
    GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint width, height, numerator, denominator;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    gst_structure_get_fraction(structure, "framerate", &numerator, &denominator);
    media_library_return config_status = self->medialib_multi_resize->set_input_video_config(width, height, numerator / denominator);

    if (config_status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library Multi-Resize could not accept sink caps, failed on error %d", config_status);
        ret = FALSE;
    }

    return ret;
}

static gboolean gst_hailo_multi_resize_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(parent);
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
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
        GST_DEBUG_OBJECT(self, "Received custom event from sinkpad");

        const GstStructure *structure = gst_event_get_structure(event);
        if (structure == NULL)
        {
            return FALSE;
        }

        if (gst_structure_has_name(structure, ROTATION_EVENT_NAME))
        {
            guint rotation;
            if (!gst_structure_get_uint(structure, ROTATION_EVENT_PROP_NAME, &rotation))
            {
                GST_ERROR_OBJECT(self, "Failed receiving rotation value from custom event");
                gst_event_unref(event);
                return FALSE;
            }

            GST_DEBUG_OBJECT(self, "Received custom rotation event from sinkpad: rotation %d", rotation);
            if(self->medialib_multi_resize->set_output_rotation((rotation_angle_t)rotation) != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to set rotation value");
                gst_event_unref(event);
                return FALSE;
            }
            gst_event_unref(event);
        }
        else
        {
            // for unknown events, call default handler
            ret = gst_pad_event_default(pad, parent, event);
            if (!ret)
            {
                GST_ERROR_OBJECT(self, "Failed to handle custom event");
                return FALSE;
            }
        }
        ret = TRUE;
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

static gboolean intersect_peer_srcpad_caps(GstHailoMultiResize *self, GstPad *sinkpad, GstPad *srcpad, output_resolution_t &output_res)
{
    GstCaps *query_caps, *intersect_caps, *peercaps;
    gboolean ret = TRUE;
    gchar *srcpad_name = gst_pad_get_name(srcpad);

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
        GST_ERROR_OBJECT(self, "Failed to intersect caps - with srcpad %s and requested width %ld height %ld and framerate %d", srcpad_name, output_res.dimensions.destination_width, output_res.dimensions.destination_height, output_res.framerate);
        ret = FALSE;
    }

    if (peercaps)
        gst_caps_unref(peercaps);

    g_free(srcpad_name);
    gst_caps_unref(intersect_caps);
    gst_caps_unref(query_caps);
    return ret;
}

static gboolean gst_hailo_handle_caps_query(GstHailoMultiResize *self, GstPad *pad, GstQuery *query)
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
    if (self->medialib_multi_resize == nullptr)
    {
        GST_ERROR_OBJECT(pad, "self->medialib_multi_resize nullptr at time of caps query");
        return FALSE;
    }
    output_video_config_t &output_config = self->medialib_multi_resize->get_output_video_config();
    for (guint i = 0; i < self->srcpads->size(); i++)
    {
        if (!intersect_peer_srcpad_caps(self, pad, self->srcpads->at(i), output_config.resolutions[i]))
        {
            gst_caps_unref(caps_result);
            return FALSE;
        }
    }

    // set the caps result
    gst_query_set_caps_result(query, caps_result);
    gst_caps_unref(caps_result);

    return TRUE;
}

static gboolean
gst_hailo_multi_resize_sink_query(GstPad *pad,
                                  GstObject *parent, GstQuery *query)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(parent);
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

static void gst_hailo_multi_resize_finalize(GObject *object)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(object);
    GST_DEBUG_OBJECT(self, "finalize");
    if (self->config_file_path)
    {
        g_free(self->config_file_path);
        self->config_file_path = NULL;
    }

    if (self->medialib_multi_resize)
    {
        self->medialib_multi_resize.reset();
        self->medialib_multi_resize = NULL;
    }
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_hailo_multi_resize_dispose(GObject *object)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(object);
    GST_DEBUG_OBJECT(self, "dispose");

    gst_hailo_multi_resize_reset(self);

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
gst_hailo_multi_resize_release_srcpad(GstPad *pad, GstHailoMultiResize *self)
{
    if (pad != NULL)
    {
        gchar *name = gst_pad_get_name(pad);
        GST_DEBUG_OBJECT(self, "Releasing srcpad %s", name);
        g_free(name);
        gst_pad_set_active(pad, FALSE);
        gst_element_remove_pad(GST_ELEMENT_CAST(self), pad);
    }
}

static void gst_hailo_multi_resize_reset_properties(GstHailoMultiResize *self)
{
    if (self->config_file_path)
    {
        g_free(self->config_file_path);
        self->config_file_path = NULL;
    }

    if (self->config_string)
    {
        g_free(self->config_string);
        self->config_string = NULL;
    }
}

static void
gst_hailo_multi_resize_reset(GstHailoMultiResize *self)
{
    GST_DEBUG_OBJECT(self, "reset");
    if (self->sinkpad != NULL)
    {
        self->sinkpad = NULL;
    }

    gst_hailo_multi_resize_reset_properties(self);

    for (guint i = 0; i < self->srcpads->size(); i++)
    {
        GstPad *srcpad = self->srcpads->at(i);
        if (srcpad != NULL)
        {
            gst_hailo_multi_resize_release_srcpad(srcpad, self);
        }
    }
    self->srcpads->clear();
    self->srcpads = nullptr;
}

static void gst_hailo_multi_resize_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(object);

    gst_hailo_multi_resize_reset_properties(self);

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        G_VALUE_REPLACE_STRING(self->config_file_path, value);
        GST_DEBUG_OBJECT(self, "config_file_path: %s", self->config_file_path);
        std::string config_string = gstmedialibcommon::read_json_string_from_file(self->config_file_path);

        if (self->medialib_multi_resize == nullptr)
        {
            gst_hailo_multi_resize_create(self, config_string);
        }
        else
        {
            media_library_return config_status = self->medialib_multi_resize->configure(config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        G_VALUE_REPLACE_STRING(self->config_string, value);
        std::string config_string = std::string(self->config_string);
        gstmedialibcommon::strip_string_syntax(config_string);

        if (self->medialib_multi_resize == nullptr)
        {
            gst_hailo_multi_resize_create(self, config_string);
        }
        else
        {
            media_library_return config_status = self->medialib_multi_resize->configure(config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG:
    {
        if (self->medialib_multi_resize)
        {
            multi_resize_config_t *multi_resize_config = static_cast<multi_resize_config_t *>(g_value_get_pointer(value));
            if (self->medialib_multi_resize->configure(*multi_resize_config) != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to configure multi resize with multi_resize_config_t object");
            }
            else
            {
                self->multi_resize_config = std::make_shared<multi_resize_config_t>(*multi_resize_config);
            }
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void
gst_hailo_multi_resize_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(object);

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
        g_value_set_string(value, self->config_string);
        break;
    }
    case PROP_PRIVACY_MASK:
    {
        if (self->medialib_multi_resize != nullptr)
            g_value_set_pointer(value, self->medialib_multi_resize->get_privacy_mask_blender().get());
        else
            g_value_set_pointer(value, NULL);
        break;
    }
    case PROP_CONFIG:
    {
        if (self->medialib_multi_resize != nullptr) {
            self->multi_resize_config = std::make_shared<multi_resize_config_t>(self->medialib_multi_resize->get_multi_resize_configs());
        } else {
            self->multi_resize_config = std::make_shared<multi_resize_config_t>();
        }
        g_value_set_pointer(value, self->multi_resize_config.get());
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstPad *
gst_hailo_multi_resize_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps)
{
    GstPad *srcpad;
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(element);
    GST_DEBUG_OBJECT(self, "Request new pad name: %s", name);

    GST_OBJECT_LOCK(self);
    srcpad = gst_pad_new_from_template(templ, name);
    GST_OBJECT_UNLOCK(self);

    gst_pad_set_active(srcpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(self), srcpad);
    self->srcpads->emplace_back(srcpad);

    return srcpad;
}

static gboolean
gst_hailo_multi_resize_create(GstHailoMultiResize *self, std::string config_string)
{
    tl::expected<MediaLibraryMultiResizePtr, media_library_return> multi_resize = MediaLibraryMultiResize::create(config_string);
    if (!multi_resize.has_value())
    {
        GST_ERROR_OBJECT(self, "Frontend Multi-Resize configuration error: %d", multi_resize.error());
        throw std::runtime_error("Frontend Multi-Resize failed to configure, check config file.");
    }

    self->medialib_multi_resize = multi_resize.value();

    // set event callbacks
    MediaLibraryMultiResize::callbacks_t callbacks;
    callbacks.on_output_resolutions_change = [self](std::vector<output_resolution_t> &outputs_res)
    {
        // initialize caps negotiation to be passed downstream
        gst_hailo_multi_resize_on_output_caps_changed(self, outputs_res);
    };
    self->medialib_multi_resize->observe(callbacks);

    return TRUE;
}

static void
gst_hailo_multi_resize_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(element);
    gchar *name = gst_pad_get_name(pad);
    GST_DEBUG_OBJECT(self, "Release pad: %s", name);
    g_free(name);
    gst_element_remove_pad(element, pad);
}
