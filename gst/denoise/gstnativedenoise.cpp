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
#include "gstnativedenoise.hpp"
#include "common/gstmedialibcommon.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "gst/gstbuffer.h"
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include <gst/video/video.h>
#include <tl/expected.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_hailo_denoise_debug);
#define GST_CAT_DEFAULT gst_hailo_denoise_debug

#define DENOISE_EVENT_NAME "DENOISE_STATUS_EVENT"
#define ROTATION_EVENT_NAME "HAILO_ROTATION_EVENT"
#define ROTATION_EVENT_PROP_NAME "rotation"

// Pad Templates
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

#define _do_init                                                                                                       \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_denoise_debug, "hailodenoise", 0, "Hailo low light enhancement element");

#define gst_hailo_denoise_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoDenoise, gst_hailo_denoise, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_denoise_finalize(GObject *object);
static void gst_hailo_denoise_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_denoise_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailo_denoise_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstStateChangeReturn gst_hailo_denoise_change_state(GstElement *element, GstStateChange transition);
static gboolean send_denoise_event(GstPad *srcpad, gboolean denoise_status);
static gboolean gst_hailo_denoise_create(GstHailoDenoise *self, const std::string &config_string);
static gboolean gst_hailo_denoise_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static void gst_hailo_denoise_queue_buffer(GstHailoDenoise *self, GstBuffer *buffer);
static GstBuffer *gst_hailo_denoise_dequeue_buffer(GstHailoDenoise *self);
static void gst_hailo_denoise_clear_staging_queue(GstHailoDenoise *self);
static void gst_hailo_denoise_deploy_buffer(GstHailoDenoise *self, HailoMediaLibraryBufferPtr hailo_buffer);

enum
{
    PROP_PAD_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_CONFIG,
};

static void gst_hailo_denoise_class_init(GstHailoDenoiseClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    // Pad templates
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    // Set metadata
    gst_element_class_set_static_metadata(element_class, "low light enhancement", "Hailo/Media-Library",
                                          "Denoising element for low light enhancement.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailo_denoise_set_property;
    gobject_class->get_property = gst_hailo_denoise_get_property;
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_denoise_finalize);

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
        g_param_spec_pointer("config", "Denoise config", "Fronted config as denoise_config_t",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailo_denoise_change_state);
}

static void gst_hailo_denoise_init(GstHailoDenoise *denoise)
{
    GST_DEBUG_OBJECT(denoise, "init");
    denoise->params = new GstHailoDenoiseParams();

    denoise->params->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    denoise->params->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(denoise->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_chain));
    gst_pad_set_query_function(denoise->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_sink_query));

    GST_PAD_SET_PROXY_CAPS(denoise->params->sinkpad);
    gst_element_add_pad(GST_ELEMENT(denoise), denoise->params->sinkpad);
    gst_element_add_pad(GST_ELEMENT(denoise), denoise->params->srcpad);
}

static void gst_hailo_denoise_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(object);

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH: {
        self->params->config_file_path = glib_cpp::get_string_from_gvalue(value);
        GST_DEBUG_OBJECT(self, "config_file_path: %s", self->params->config_file_path.c_str());
        std::string config_string = gstmedialibcommon::read_json_string_from_file(self->params->config_file_path);

        if (self->params->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self, config_string);
        }
        else
        {
            media_library_return config_status = self->params->medialib_denoise->configure(config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG_STRING: {
        self->params->config_string = glib_cpp::get_string_from_gvalue(value);
        std::string config_string = self->params->config_string;
        gstmedialibcommon::strip_string_syntax(config_string);

        if (self->params->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self, config_string);
        }
        else
        {
            media_library_return config_status = self->params->medialib_denoise->configure(config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG: {
        if (self->params->medialib_denoise)
        {
            denoise_config_t *denoise_config = static_cast<denoise_config_t *>(g_value_get_pointer(value));
            hailort_t hailort_configs = self->params->medialib_denoise->get_hailort_configs();

            if (self->params->medialib_denoise->configure(*denoise_config, hailort_configs) != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to configure dewarp with denoise_config_t object");
            }
            else
            {
                self->params->denoise_config = *denoise_config;
            }
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_hailo_denoise_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(object);

    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH: {
        g_value_set_string(value, self->params->config_file_path.c_str());
        break;
    }
    case PROP_CONFIG_STRING: {
        g_value_set_string(value, self->params->config_string.c_str());
        break;
    }
    case PROP_CONFIG: {
        if (self->params->medialib_denoise != nullptr)
        {
            self->params->denoise_config = self->params->medialib_denoise->get_denoise_configs();
        }
        else
        {
            self->params->denoise_config = denoise_config_t();
        }
        g_value_set_pointer(value, &self->params->denoise_config);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_hailo_denoise_create(GstHailoDenoise *self, const std::string &config_string)
// gst_hailo_denoise_create(GstHailoDenoise *self)
{
    auto medialb_denoise = std::make_shared<MediaLibraryPostIspDenoise>();
    if (medialb_denoise->configure(config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to config denoise");
        return FALSE;
    }

    self->params->medialib_denoise = medialb_denoise;
    // set event callbacks
    MediaLibraryPostIspDenoise::callbacks_t callbacks;
    callbacks.on_buffer_ready = [self](HailoMediaLibraryBufferPtr out_buf) {
        gst_hailo_denoise_deploy_buffer(self, out_buf);
    };
    callbacks.on_enable_changed = [self](bool enabled) {
        if (!enabled)
        {
            self->params->m_flushing = true;
            self->params->m_condvar.notify_one();
            gst_hailo_denoise_clear_staging_queue(self);
        }
    };
    callbacks.send_event = [self](bool enabled) {
        // send event downstream to notify about denoise enabled or disabled
        if (GST_STATE(self) == GST_STATE_PLAYING || GST_STATE(self) == GST_STATE_PAUSED)
        {
            send_denoise_event(self->params->srcpad, enabled);
        }
    };
    self->params->medialib_denoise->observe(callbacks);

    return TRUE;
}

static GstFlowReturn gst_hailo_denoise_push_output_frame(GstHailoDenoise *self, HailoMediaLibraryBufferPtr hailo_buffer,
                                                         GstBuffer *buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;

    if (hailo_buffer->buffer_data == nullptr)
    {
        GST_ERROR_OBJECT(self, "Trying to push null output frame");
        ret = GST_FLOW_ERROR;
        return ret;
    }

    if (GST_PAD_IS_FLUSHING(self->params->srcpad))
    {
        GST_WARNING_OBJECT(self, "Pad is flushing, not pushing buffer");
        return GST_FLOW_OK;
    }

    // Get caps from srcpad
    GstCaps *caps = gst_pad_get_current_caps(self->params->srcpad);

    if (!caps)
    {
        GST_ERROR_OBJECT(self, "Failed to get caps from srcpad name %s",
                         glib_cpp::get_name(self->params->srcpad).c_str());
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Creating GstBuffer from dsp buffer");
    GstBuffer *gst_outbuf = gst_buffer_from_hailo_buffer(hailo_buffer, caps);
    gst_caps_unref(caps);
    if (!gst_outbuf)
    {
        GST_ERROR_OBJECT(self, "Failed to create GstBuffer from media library buffer");
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", glib_cpp::get_name(self->params->srcpad).c_str());
    gst_outbuf->pts = GST_BUFFER_PTS(buffer);
    gst_outbuf->offset = GST_BUFFER_OFFSET(buffer);
    gst_outbuf->duration = GST_BUFFER_DURATION(buffer);
    // add vsm meta?
    gst_pad_push(self->params->srcpad, gst_outbuf);

    return ret;
}

static GstFlowReturn gst_hailo_denoise_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(parent);
    GstFlowReturn ret = GST_FLOW_OK;

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    // If Denoise disbled, just push the buffer to srcpad
    if (!self->params->medialib_denoise->is_enabled())
    {
        GST_DEBUG_OBJECT(self, "Post ISP Denoise disabled, pushing buffer to srcpad");
        gst_pad_push(self->params->srcpad, buffer);
        return ret;
    }

    GstCaps *input_caps = gst_pad_get_current_caps(pad);

    // Extract frame dimensions from caps for dynamic buffer pool creation
    GstVideoInfo *video_info;
    video_info = gst_video_info_new_from_caps(input_caps);
    if (!video_info)
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to get video info from caps");
        return GST_FLOW_ERROR;
    }

    // Set input dimensions for dynamic buffer pool creation
    // This should be called once when processing the first frame
    if (self->params->medialib_denoise->set_input_dimensions(video_info->width, video_info->height) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to set input dimensions for denoise: %dx%d", video_info->width,
                         video_info->height);
        gst_video_info_free(video_info);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Set denoise input dimensions: %dx%d", video_info->width, video_info->height);
    gst_video_info_free(video_info);

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }
    gst_caps_unref(input_caps);

    HailoMediaLibraryBufferPtr output_frame_ptr = std::make_shared<hailo_media_library_buffer>();

    GST_DEBUG_OBJECT(self, "Call media library handle frame - GstBuffer offset %ld", GST_BUFFER_OFFSET(buffer));
    media_library_return media_lib_ret =
        self->params->medialib_denoise->handle_frame(input_frame_ptr, output_frame_ptr);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        if (media_lib_ret == MEDIA_LIBRARY_UNINITIALIZED)
        {
            GST_DEBUG_OBJECT(self, "Post ISP Denoise disabled, pushing buffer to srcpad");
            // After getting hailo_buffer from input frame, in case of sending it we have to wrap it:
            // If we don't input_frame_ptr will be destructed and so its mmap, which might be in use in
            // a downstream gst element
            GstBuffer *mmapped_input_as_gst_buf = gst_buffer_from_hailo_buffer(input_frame_ptr, input_caps);
            gst_pad_push(self->params->srcpad, mmapped_input_as_gst_buf);
            gst_buffer_unref(buffer);
            return ret;
        }

        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    if (output_frame_ptr->buffer_data == nullptr)
    {
        GST_DEBUG_OBJECT(self, "Post ISP Denoise disabled, pushing buffer to srcpad");
        GstBuffer *mmapped_input_as_gst_buf = gst_buffer_from_hailo_buffer(input_frame_ptr, input_caps);
        gst_pad_push(self->params->srcpad, mmapped_input_as_gst_buf);
        gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT(self, "Handle frame done");
    gst_hailo_denoise_queue_buffer(self, buffer);

    return ret;
}

static gboolean send_denoise_event(GstPad *srcpad, gboolean denoise_status)
{
    if (!srcpad)
    {
        GST_ERROR("Source pad is NULL, cannot send denoise event.");
        return FALSE;
    }

    GstStructure *structure = gst_structure_new(DENOISE_EVENT_NAME, "flag", G_TYPE_BOOLEAN, denoise_status, NULL);
    GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);

    if (!gst_pad_push_event(srcpad, event))
    {
        GST_ERROR("Failed to push denoise status event to srcpad");
        return FALSE;
    }

    GST_DEBUG("Denoise event sent successfully: %s", denoise_status ? "enabled" : "disabled");
    return TRUE;
}

static GstStateChangeReturn gst_hailo_denoise_change_state(GstElement *element, GstStateChange transition)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(element);
    GstStateChangeReturn ret;
    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING)
    {
        send_denoise_event(self->params->srcpad, self->params->medialib_denoise->is_enabled());
    }

    return ret;
}

static void gst_hailo_denoise_deploy_buffer(GstHailoDenoise *self, HailoMediaLibraryBufferPtr hailo_buffer)
{
    GstBuffer *buffer = gst_hailo_denoise_dequeue_buffer(self);
    if (!buffer)
    {
        if (!self->params->m_flushing)
            GST_ERROR_OBJECT(self, "Failed to dequeue buffer");
        return;
    }

    gst_hailo_denoise_push_output_frame(self, hailo_buffer, buffer);
    gst_buffer_unref(buffer);
}

static gboolean gst_hailo_denoise_sink_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(parent);
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
    default: {
        /* just call the default handler */
        ret = gst_pad_query_default(pad, parent, query);
        break;
    }
    }
    return ret;
}

static void gst_hailo_denoise_finalize(GObject *object)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(object);
    GST_DEBUG_OBJECT(self, "finalize");

    self->params->m_flushing = true;
    gst_hailo_denoise_clear_staging_queue(self);

    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_hailo_denoise_queue_buffer(GstHailoDenoise *self, GstBuffer *buffer)
{
    std::unique_lock<std::mutex> lock(self->params->m_mutex);
    self->params->m_condvar.wait(lock,
                                 [self] { return self->params->m_staging_queue.size() < self->params->m_queue_size; });
    self->params->m_staging_queue.push(buffer);
    self->params->m_condvar.notify_one();
}

static GstBuffer *gst_hailo_denoise_dequeue_buffer(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(self->params->m_mutex);
    self->params->m_condvar.wait(lock,
                                 [self] { return !self->params->m_staging_queue.empty() || self->params->m_flushing; });
    if (self->params->m_staging_queue.empty())
    {
        return nullptr;
    }
    GstBuffer *buffer = self->params->m_staging_queue.front();
    self->params->m_staging_queue.pop();
    self->params->m_condvar.notify_one();
    return buffer;
}

static void gst_hailo_denoise_clear_staging_queue(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(self->params->m_mutex);
    while (!self->params->m_staging_queue.empty())
    {
        GstBuffer *buffer = self->params->m_staging_queue.front();
        self->params->m_staging_queue.pop();
        gst_buffer_unref(buffer);
    }
    self->params->m_condvar.notify_one();
}
