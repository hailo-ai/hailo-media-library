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
#include "gst/gstcaps.h"
#include "gstmedialibptrs.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library_types.hpp"
#include "multi_resize.hpp"
#include <algorithm>
#include <gst/video/video.h>
#include <tl/expected.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_hailo_multi_resize_debug);
#define GST_CAT_DEFAULT gst_hailo_multi_resize_debug

#define DENOISE_EVENT_NAME "DENOISE_STATUS_EVENT"
#define DO_FLIP_ROTATE_EVENT_NAME "HAILO_DO_FLIP_ROTATE_EVENT"
#define DO_FLIP_ROTATE_PROP_NAME "do-flip-rotate"
#define FLIP_EVENT_NAME "HAILO_FLIP_EVENT"
#define FLIP_EVENT_PROP_NAME "flip"
#define ROTATION_EVENT_NAME "HAILO_ROTATION_EVENT"
#define ROTATION_EVENT_PROP_NAME "rotation"

#define SRCPAD_NAME_FROM_STREAM_ID(stream_id) ("src_" + stream_id)

// Pad Templates
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u", GST_PAD_SRC, GST_PAD_REQUEST, GST_STATIC_CAPS_ANY);

#define _do_init                                                                                                       \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_multi_resize_debug, "hailomultiresize", 0, "Hailo Multi Resize element");

#define gst_hailo_multi_resize_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoMultiResize, gst_hailo_multi_resize, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_multi_resize_set_property(GObject *object, guint property_id, const GValue *value,
                                                GParamSpec *pspec);
static void gst_hailo_multi_resize_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailo_multi_resize_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer);
static GstPad *gst_hailo_multi_resize_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                      const GstCaps *caps);
static void gst_hailo_multi_resize_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailo_multi_resize_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static void gst_hailo_multi_resize_dispose(GObject *object);
static void gst_hailo_multi_resize_finalize(GObject *object);
static void gst_hailo_multi_resize_reset(GstHailoMultiResize *self);
static void gst_hailo_multi_resize_release_srcpad(GstPad *pad, GstHailoMultiResize *self);
static GstStateChangeReturn gst_hailo_multi_resize_change_state(GstElement *element, GstStateChange transition);

static gboolean gst_hailo_handle_caps_query(GstHailoMultiResize *self, GstPad *pad, GstQuery *query);
static gboolean gst_hailo_set_all_srcpad_caps(GstHailoMultiResize *self,
                                              const config_application_input_streams_t &outputs_config);

enum
{
    PROP_PAD_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_CONFIG,
};

static void gst_hailo_multi_resize_class_init(GstHailoMultiResizeClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class = (GObjectClass *)klass;
    gstelement_class = (GstElementClass *)klass;

    gobject_class->set_property = gst_hailo_multi_resize_set_property;
    gobject_class->get_property = gst_hailo_multi_resize_get_property;
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_dispose);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_finalize);

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_FILE_PATH,
        g_param_spec_string("config-file-path", "Config file path", "JSON config file path to load", "",
                            (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                          GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_STRING,
        g_param_spec_string("config-string", "Config string", "JSON config string to load", "",
                            (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                          GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG,
        g_param_spec_pointer("config", "multi resize config", "Multi Resize config as multi_resize_config_t",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    // Pad templates
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);
    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

    gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_change_state);
    gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_request_new_pad);
    gstelement_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_release_pad);

    gst_element_class_set_static_metadata(gstelement_class, "1 to N multiple resize using dsp", "Hailo Multi Resize",
                                          "1 to N multiple resize using dsp", "Hailo");
}

static void gst_hailo_multi_resize_init(GstHailoMultiResize *multi_resize)
{
    GST_DEBUG_OBJECT(multi_resize, "init");
    multi_resize->params = new GstHailoMultiResizeParams();

    auto multi_resize_exp = MediaLibraryMultiResize::create();
    if (!multi_resize_exp.has_value())
    {
        GST_ERROR_OBJECT(multi_resize, "Multi Resize creation error: %d", multi_resize_exp.error());
        return;
    }

    multi_resize->params->medialib_multi_resize = multi_resize_exp.value();
    multi_resize->params->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");

    gst_pad_set_chain_function(multi_resize->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_chain));
    gst_pad_set_query_function(multi_resize->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_multi_resize_sink_query));

    GST_PAD_SET_PROXY_CAPS(multi_resize->params->sinkpad.get());
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(multi_resize), multi_resize->params->sinkpad);

    MediaLibraryMultiResize::callbacks_t callbacks;
    callbacks.on_output_resolutions_change = [multi_resize](const config_application_input_streams_t &outputs_config) {
        guint num_of_srcpads = multi_resize->params->srcpads_by_names.size();
        if (num_of_srcpads > outputs_config.resolutions.size())
        {
            GST_ERROR_OBJECT(multi_resize, "Number of srcpads (%d) exceeds number of output resolutions (%ld)",
                             num_of_srcpads, outputs_config.resolutions.size());
            return;
        }

        GST_INFO_OBJECT(multi_resize, "Output resolutions change detected, updating srcpad caps");
        if (!gst_hailo_set_all_srcpad_caps(multi_resize, outputs_config))
        {
            GST_ERROR_OBJECT(multi_resize, "Failed to set all srcpad caps on output resolutions change");
            return;
        }

        multi_resize->params->outputs_config = outputs_config;
    };
    multi_resize->params->medialib_multi_resize->observe(callbacks);
}

static GstFlowReturn gst_hailo_multi_resize_push_output_frames(GstHailoMultiResize *self,
                                                               MultiResizeOutputBuffersMap &output_frames,
                                                               GstBufferPtr &buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;
    guint output_frames_size = output_frames.size();
    if (output_frames_size < self->params->srcpads_by_names.size())
    {
        GST_ERROR_OBJECT(self, "Number of output frames (%d) is lower than the number of srcpads (%ld)",
                         output_frames_size, self->params->srcpads_by_names.size());
        return GST_FLOW_ERROR;
    }
    else if (output_frames_size > self->params->srcpads_by_names.size())
    {
        GST_WARNING_OBJECT(self, "Number of output frames (%d) is higher than the number of srcpads (%ld)",
                           output_frames_size, self->params->srcpads_by_names.size());
    }

    for (auto &[stream_id, hailo_buffer] : output_frames)
    {
        if (!hailo_buffer || hailo_buffer->buffer_data == nullptr)
        {
            GST_DEBUG_OBJECT(self, "Output frame for stream id %s is null, skipping", stream_id.c_str());
            continue;
        }

        if (self->params->srcpad_names_by_stream_id.find(stream_id) == self->params->srcpad_names_by_stream_id.end())
        {
            GST_TRACE_OBJECT(self, "Stream id %s has no connected srcpad (e.g., motion detection), skipping forward",
                             stream_id.c_str());
            continue;
        }

        auto srcpad_name = self->params->srcpad_names_by_stream_id.at(stream_id);
        GstPad *srcpad = self->params->srcpads_by_names.at(srcpad_name);
        if (!srcpad)
        {
            GST_WARNING_OBJECT(self, "Failed to get srcpad %s", srcpad_name.c_str());
            continue;
        }
        if (GST_PAD_IS_FLUSHING(srcpad))
        {
            GST_WARNING_OBJECT(self, "srcpad %s is flushing", srcpad_name.c_str());
            continue;
        }
        // Get caps from srcpad
        GstCapsPtr caps = gst_pad_get_current_caps(srcpad);

        if (!caps)
        {
            GST_ERROR_OBJECT(self, "Failed to get caps from srcpad name %s", srcpad_name.c_str());
            ret = GST_FLOW_ERROR;
            continue;
        }

        GST_DEBUG_OBJECT(self, "Creating GstBuffer from dsp buffer, stream id %s, srcpad name %s, caps %s",
                         stream_id.c_str(), srcpad_name.c_str(), gst_caps_to_string(caps));
        GstBufferPtr outbuf = gst_buffer_from_hailo_buffer(hailo_buffer, caps);
        if (!outbuf)
        {
            GST_ERROR_OBJECT(self, "Failed to create GstBuffer from dsp buffer");
            ret = GST_FLOW_ERROR;
            continue;
        }

        auto it = std::find_if(self->params->outputs_config.resolutions.begin(),
                               self->params->outputs_config.resolutions.end(),
                               [&stream_id](const output_resolution_t &res) { return res.stream_id == stream_id; });

        if (it == self->params->outputs_config.resolutions.end())
        {
            GST_ERROR_OBJECT(self, "Failed to find output resolution for stream id %s", stream_id.c_str());
            ret = GST_FLOW_ERROR;
            continue;
        }

        GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", srcpad_name.c_str());
        outbuf->pts = GST_BUFFER_PTS(buffer);
        outbuf->offset = GST_BUFFER_OFFSET(buffer);
        // Duration changes according to the requested output framerate
        outbuf->duration = GST_BUFFER_DURATION(buffer) * (hailo_buffer->isp_ae_fps / it->framerate);
        glib_cpp::ptrs::push_buffer_to_pad(srcpad, outbuf);
    }

    return ret;
}

static GstFlowReturn gst_hailo_multi_resize_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer)
{
    GstBufferPtr buffer = gst_buffer;
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(parent);

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    GstCapsPtr input_caps = gst_pad_get_current_caps(pad);

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }

    MultiResizeOutputBuffersMap output_frames;
    GST_DEBUG_OBJECT(self, "Call media library handle frame - GstBuffer offset %ld", GST_BUFFER_OFFSET(buffer));
    media_library_return media_lib_ret =
        self->params->medialib_multi_resize->handle_frame(input_frame_ptr, output_frames);

    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Handle frame done");
    return gst_hailo_multi_resize_push_output_frames(self, output_frames, buffer);
}

static std::vector<GstCapsPtr> gst_hailo_create_caps_from_output_config(
    GstHailoMultiResize *self, const config_application_input_streams_t &outputs_config)
{
    const HailoFormat &hailo_format = outputs_config.format;
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
        return {};
    }

    std::vector<GstCapsPtr> all_outputs_caps;
    for (const auto &output_res : outputs_config.resolutions)
    {
        GstCapsPtr output_caps;
        guint framerate = (guint)output_res.framerate;
        // TODO (MSW-4090): support 0 fps --> disable stream
        if (framerate == 0)
            framerate = 1;

        GST_DEBUG_OBJECT(self, "Creating caps - width = %ld height = %ld framerate = %d",
                         output_res.dimensions.destination_width, output_res.dimensions.destination_height,
                         output_res.framerate);
        output_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format.c_str(), "width", G_TYPE_INT,
                                          (guint)output_res.dimensions.destination_width, "height", G_TYPE_INT,
                                          (guint)output_res.dimensions.destination_height, "framerate",
                                          GST_TYPE_FRACTION, framerate, 1, NULL);

        all_outputs_caps.push_back(std::move(output_caps));
    }

    return all_outputs_caps;
}

static gboolean gst_hailo_set_all_srcpad_caps(GstHailoMultiResize *self,
                                              const config_application_input_streams_t &outputs_config)
{
    GstCapsPtr caps_result, outcaps;
    std::vector<GstCapsPtr> query_caps;
    query_caps = gst_hailo_create_caps_from_output_config(self, outputs_config);

    // Query the peer srcpad to obtain wanted resolution
    if (query_caps.size() != self->params->srcpads_by_names.size())
    {
        GST_ERROR_OBJECT(self, "Number of query caps (%ld) does not match number of srcpads (%ld)", query_caps.size(),
                         self->params->srcpads_by_names.size());
        return FALSE;
    }
    for (size_t i = 0; i < query_caps.size(); ++i)
    {
        const std::string &stream_id = outputs_config.resolutions[i].stream_id;
        if (self->params->srcpad_names_by_stream_id.find(stream_id) == self->params->srcpad_names_by_stream_id.end())
        {
            GST_INFO_OBJECT(self, "Assigning unconnected srcpad for stream id %s", stream_id.c_str());
            if (self->params->not_connected_srcpad_names.empty())
            {
                GST_ERROR_OBJECT(self, "No available srcpad names to assign for stream id %s", stream_id.c_str());
                return FALSE;
            }
            self->params->srcpad_names_by_stream_id[stream_id] = self->params->not_connected_srcpad_names.front();
            self->params->not_connected_srcpad_names.pop();
        }
        auto srcpad_name = self->params->srcpad_names_by_stream_id.at(stream_id);
        auto srcpad = self->params->srcpads_by_names.at(srcpad_name);
        caps_result = gst_pad_peer_query_caps(srcpad, query_caps.at(i));
        outcaps = glib_cpp::ptrs::fixate_caps(caps_result);

        // Check if outcaps intersects of query_caps
        GST_DEBUG_OBJECT(self, "Caps event - fixated peer srcpad caps %" GST_PTR_FORMAT, outcaps.get());

        if (gst_caps_is_empty(outcaps) || !gst_caps_is_fixed(outcaps))
        {
            GST_ERROR_OBJECT(self,
                             "Caps event - set caps is not possible, Failed to match required caps with srcpad %s",
                             srcpad_name.c_str());
            return FALSE;
        }

        // set the caps on the peer srcpad
        gboolean srcpad_set_caps_result = gst_pad_set_caps(srcpad, outcaps);
        if (!srcpad_set_caps_result)
        {
            GST_ERROR_OBJECT(self, "Failed to set caps on srcpad %s", srcpad_name.c_str());
            return FALSE;
        }
        GST_INFO_OBJECT(self, "Set srcpad %s for stream id %s caps: %" GST_PTR_FORMAT, srcpad_name.c_str(),
                        stream_id.c_str(), outcaps.get());
    }

    if (!self->params->not_connected_srcpad_names.empty())
    {
        GST_ERROR_OBJECT(self, "Not all srcpads were connected, remaining srcpads count: %ld",
                         self->params->not_connected_srcpad_names.size());
    }

    return TRUE;
}

static gboolean gst_hailo_handle_caps_query(GstHailoMultiResize *self, GstPad *pad, GstQuery *query)
{
    // get pad name and direction
    GstPadDirection pad_direction = gst_pad_get_direction(pad);
    auto pad_name = glib_cpp::get_name(pad);
    GST_DEBUG_OBJECT(pad, "Received caps query from sinkpad name %s direction %d", pad_name.c_str(), pad_direction);
    GstCapsPtr caps_result, allowed_caps, qcaps;
    /* we should report the supported caps here which are all */
    allowed_caps = gst_pad_get_pad_template_caps(pad);
    qcaps = glib_cpp::ptrs::parse_query_caps(query);
    if (qcaps && allowed_caps && !gst_caps_is_any(allowed_caps))
    {
        GST_DEBUG_OBJECT(pad, "qcaps %" GST_PTR_FORMAT, qcaps.get());
        // caps query - intersect template caps (allowed caps) with incomming caps query
        caps_result = gst_caps_intersect(allowed_caps, qcaps);
        GST_DEBUG_OBJECT(pad, "Caps intersection completed");
    }
    else
    {
        // no caps query - return template caps
        caps_result = std::move(allowed_caps);
    }

    GST_DEBUG_OBJECT(pad, "Returning allowed caps template  %" GST_PTR_FORMAT, caps_result.get());
    if (self->params->medialib_multi_resize == nullptr)
    {
        GST_ERROR_OBJECT(pad, "self->params->medialib_multi_resize nullptr at time of caps query");
        return FALSE;
    }

    // set the caps result
    gst_query_set_caps_result(query, caps_result);
    return TRUE;
}

static gboolean gst_hailo_multi_resize_sink_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(parent);
    gboolean ret;

    switch (GST_QUERY_TYPE(query))
    {
    case GST_QUERY_ALLOCATION: {
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        ret = gst_pad_query_default(pad, parent, query);
        GST_DEBUG_OBJECT(self, "Received allocation query from sinkpad, returning %d", ret);
        break;
    }
    case GST_QUERY_CAPS: {
        ret = gst_hailo_handle_caps_query(self, pad, query);
        break;
    }
    case GST_QUERY_ACCEPT_CAPS: {
        GstCapsPtr caps = glib_cpp::ptrs::parse_query_accept_caps(query);
        GST_DEBUG_OBJECT(self, "received accept caps query from sinkpad, caps %" GST_PTR_FORMAT, caps.get());
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

static void gst_hailo_multi_resize_finalize(GObject *object)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(object);
    GST_DEBUG_OBJECT(self, "finalize");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_hailo_multi_resize_dispose(GObject *object)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(object);
    GST_DEBUG_OBJECT(self, "dispose");

    gst_hailo_multi_resize_reset(self);
    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void gst_hailo_multi_resize_release_srcpad(GstPad *pad, GstHailoMultiResize *self)
{
    if (pad != NULL)
    {
        auto name = glib_cpp::get_name(pad);
        GST_DEBUG_OBJECT(self, "Releasing srcpad %s", name.c_str());
        gst_pad_set_active(pad, FALSE);
        gst_element_remove_pad(GST_ELEMENT_CAST(self), pad);
    }
}

static void gst_hailo_multi_resize_reset(GstHailoMultiResize *self)
{
    GST_DEBUG_OBJECT(self, "RESET CALLED. Releasing all srcpads");
    for (auto &srcpad_pair : self->params->srcpads_by_names)
    {
        GstPad *srcpad = srcpad_pair.second;
        if (srcpad != NULL)
        {
            gst_hailo_multi_resize_release_srcpad(srcpad, self);
        }
    }
}

static void gst_hailo_multi_resize_set_property(GObject *object, guint property_id, const GValue *, GParamSpec *pspec)
{
    switch (property_id)
    {
    // Handle property assignments here
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_hailo_multi_resize_get_property(GObject *object, guint property_id, GValue *, GParamSpec *pspec)
{
    switch (property_id)
    {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstPad *gst_hailo_multi_resize_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                      const GstCaps *)
{
    GstPadPtr srcpad;
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(element);
    GST_OBJECT_LOCK(self);
    srcpad = gst_pad_new_from_template(templ, name);
    GST_OBJECT_UNLOCK(self);

    const char *srcpad_name = glib_cpp::ptrs::get_pad_name(srcpad);

    gst_pad_set_active(srcpad, TRUE);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(self), srcpad);
    self->params->srcpads_by_names[srcpad_name] = srcpad;
    self->params->not_connected_srcpad_names.push(srcpad_name);
    srcpad.set_auto_unref(false);

    GST_DEBUG_OBJECT(self, "Create srcpad name: %s(%s)", srcpad_name, name);
    return srcpad;
}

static void gst_hailo_multi_resize_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(element);
    auto srcpad_name = glib_cpp::get_name(pad);
    GST_DEBUG_OBJECT(self, "Release pad: %s", srcpad_name.c_str());
    if (self->params->srcpads_by_names.contains(srcpad_name))
    {
        self->params->srcpads_by_names.erase(srcpad_name);
    }
    std::erase_if(self->params->srcpad_names_by_stream_id,
                  [srcpad_name](const auto &item) { return item.second == srcpad_name; });
    glib_cpp::ptrs::remove_pad_from_element(GST_ELEMENT(self), pad);
}

static GstStateChangeReturn gst_hailo_multi_resize_change_state(GstElement *element, GstStateChange transition)
{
    GstHailoMultiResize *self = GST_HAILO_MULTI_RESIZE(element);
    GstStateChangeReturn result = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_READY_TO_NULL: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_NULL - cleaning multi resize state");
        self->params->medialib_multi_resize->clean_on_stop();
        break;
    }
    default:
        break;
    }

    return result;
}
