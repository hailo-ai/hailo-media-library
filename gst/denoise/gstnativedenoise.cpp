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
#include "gstmedialibptrs.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/snapshot.hpp"
#include "hailo_media_library_perfetto.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include "media_library/media_library_types.hpp"
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <limits>

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
static GstFlowReturn gst_hailo_denoise_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer);
static GstStateChangeReturn gst_hailo_denoise_change_state(GstElement *element, GstStateChange transition);
static gboolean send_denoise_event(GstPad *srcpad, gboolean denoise_status);
static gboolean gst_hailo_denoise_create(GstHailoDenoise *self, const frontend_config_t &frontend_config);
static gboolean gst_hailo_denoise_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static void gst_hailo_denoise_queue_buffer(GstHailoDenoise *self, GstBufferPtr &buffer);
static GstBufferPtr gst_hailo_denoise_dequeue_buffer(GstHailoDenoise *self);
static void gst_hailo_denoise_clear_staging_queue(GstHailoDenoise *self);
static void gst_hailo_denoise_deploy_buffer(GstHailoDenoise *self, HailoMediaLibraryBufferPtr hailo_buffer);
static GstCapsPtr gst_hailo_denoise_create_srcpad_caps(GstHailoDenoise *self);
static gboolean gst_hailo_denoise_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);

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
    denoise->params->m_config_manager = std::make_unique<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_FRONTEND);

    denoise->params->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    denoise->params->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(denoise->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_chain));
    gst_pad_set_query_function(denoise->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_sink_query));
    gst_pad_set_event_function(denoise->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_sink_event));

    GST_PAD_SET_PROXY_CAPS(denoise->params->sinkpad.get());
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(denoise), denoise->params->sinkpad);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(denoise), denoise->params->srcpad);
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

        auto frontend_config = std::make_unique<frontend_config_t>();
        if (self->params->m_config_manager->config_string_to_struct(config_string, *frontend_config) !=
            MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(self, "Failed to load config from file %s", self->params->config_file_path.c_str());
            return;
        }

        self->params->m_frontend_config = std::move(frontend_config);
        if (self->params->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self, *self->params->m_frontend_config);
        }
        else
        {
            // Always configure Post-ISP denoise so it knows when to disable
            // When bayer=true, Post-ISP will see disabled() return true and clean up resources
            GST_DEBUG_OBJECT(self, "Configure Post-ISP Denoise with config (bayer=%s)",
                             self->params->m_frontend_config->denoise_config.bayer ? "true" : "false");
            media_library_return config_status = self->params->medialib_denoise->configure(
                self->params->m_frontend_config->denoise_config, self->params->m_frontend_config->hailort_config,
                self->params->m_frontend_config->input_config);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }

    case PROP_CONFIG_STRING: {
        self->params->config_string = glib_cpp::get_string_from_gvalue(value);
        gstmedialibcommon::strip_string_syntax(self->params->config_string);

        auto frontend_config = std::make_unique<frontend_config_t>();
        if (self->params->m_config_manager->config_string_to_struct(self->params->config_string, *frontend_config) !=
            MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(self, "Failed to load config from file %s", self->params->config_file_path.c_str());
            return;
        }

        self->params->m_frontend_config = std::move(frontend_config);
        if (self->params->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self, *self->params->m_frontend_config);
        }
        else
        {
            // Always configure Post-ISP denoise so it knows when to disable
            // When bayer=true, Post-ISP will see disabled() return true and clean up resources
            GST_DEBUG_OBJECT(self, "Configure Post-ISP Denoise with config (bayer=%s)",
                             self->params->m_frontend_config->denoise_config.bayer ? "true" : "false");
            media_library_return config_status = self->params->medialib_denoise->configure(
                self->params->m_frontend_config->denoise_config, self->params->m_frontend_config->hailort_config,
                self->params->m_frontend_config->input_config);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG: {
        frontend_config_t *frontend_config = static_cast<frontend_config_t *>(g_value_get_pointer(value));
        self->params->m_frontend_config = std::make_unique<frontend_config_t>(*frontend_config);

        // Always configure Post-ISP denoise so it knows when to disable
        // When bayer=true, Post-ISP will see disabled() return true and clean up resources
        if (self->params->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self, *frontend_config);
        }
        else
        {
            GST_DEBUG_OBJECT(self, "Configure Post-ISP Denoise with config struct (bayer=%s)",
                             frontend_config->denoise_config.bayer ? "true" : "false");
            if (self->params->medialib_denoise->configure(frontend_config->denoise_config,
                                                          frontend_config->hailort_config,
                                                          frontend_config->input_config) != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to configure Post-ISP denoise with denoise_config_t object");
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
        g_value_set_pointer(value, &self->params->m_frontend_config->denoise_config);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_hailo_denoise_create(GstHailoDenoise *self, const frontend_config_t &frontend_config)
// gst_hailo_denoise_create(GstHailoDenoise *self)
{
    // Only create Post-ISP denoise if Pre-ISP is not active (bayer=false)
    if (frontend_config.denoise_config.bayer)
    {
        GST_DEBUG_OBJECT(self, "Pre-ISP denoise is active (bayer=true), not creating Post-ISP denoise instance");
        return TRUE; // Return success but don't create the instance
    }

    GST_DEBUG_OBJECT(self, "Configure Post-ISP Denoise with config struct (Pre-ISP disabled)");
    auto medialb_denoise = std::make_shared<MediaLibraryPostIspDenoise>();
    if (medialb_denoise->configure(frontend_config.denoise_config, frontend_config.hailort_config,
                                   frontend_config.input_config) != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to config Post-ISP denoise");
        return FALSE;
    }

    self->params->medialib_denoise = medialb_denoise;
    // set event callbacks
    MediaLibraryDenoise::callbacks_t callbacks;
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

static GstCapsPtr gst_hailo_denoise_create_srcpad_caps(GstHailoDenoise *self)
{
    const output_resolution_t &real_sensor_resolution = self->params->m_frontend_config->input_config.resolution;
    // Format does not change from input to output
    GST_DEBUG_OBJECT(self, "Fixing caps resolution after denoise - new width = %ld new height = %ld",
                     real_sensor_resolution.dimensions.destination_width,
                     real_sensor_resolution.dimensions.destination_height);

    GstCapsPtr caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "framerate",
                                          GST_TYPE_FRACTION, real_sensor_resolution.framerate, 1, "width", G_TYPE_INT,
                                          real_sensor_resolution.dimensions.destination_width, "height", G_TYPE_INT,
                                          real_sensor_resolution.dimensions.destination_height, NULL);
    return caps;
}

static GstFlowReturn gst_hailo_denoise_push_output_frame(GstHailoDenoise *self, HailoMediaLibraryBufferPtr hailo_buffer,
                                                         GstBufferPtr &buffer)
{
    if (hailo_buffer->buffer_data == nullptr)
    {
        GST_ERROR_OBJECT(self, "Trying to push null output frame");
        return GST_FLOW_ERROR;
    }

    if (GST_PAD_IS_FLUSHING(self->params->srcpad.get()))
    {
        GST_WARNING_OBJECT(self, "Pad is flushing, not pushing buffer");
        return GST_FLOW_OK;
    }

    // Get caps from srcpad
    GstCapsPtr caps = gst_pad_get_current_caps(self->params->srcpad);

    if (!caps)
    {
        GST_ERROR_OBJECT(self, "Failed to get caps from srcpad name %s",
                         glib_cpp::get_name(self->params->srcpad).c_str());
        return GST_FLOW_ERROR;
    }

    // denoise might require bigger image, after denoise we can return to normal image size
    hailo_buffer->buffer_data->height =
        self->params->m_frontend_config->input_config.resolution.dimensions.destination_height;

    GST_DEBUG_OBJECT(self, "Creating GstBuffer from hml buffer");
    GstBufferPtr gst_outbuf = gst_buffer_from_hailo_buffer(hailo_buffer, caps);
    if (!gst_outbuf)
    {
        GST_ERROR_OBJECT(self, "Failed to create GstBuffer from media library buffer");
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", glib_cpp::get_name(self->params->srcpad).c_str());
    gst_outbuf->pts = GST_BUFFER_PTS(buffer);
    gst_outbuf->offset = GST_BUFFER_OFFSET(buffer);
    gst_outbuf->duration = GST_BUFFER_DURATION(buffer);
    // add vsm meta?
    glib_cpp::ptrs::push_buffer_to_pad(self->params->srcpad, gst_outbuf);

    return GST_FLOW_OK;
}

static GstFlowReturn gst_hailo_denoise_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(parent);
    GstBufferPtr buffer = gst_buffer;

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    GstCapsPtr input_caps = gst_pad_get_current_caps(pad);

    HailoMediaLibraryBufferPtr input_frame_ptr = hailo_buffer_from_gst_buffer(buffer, input_caps);
    if (!input_frame_ptr)
    {
        GST_ERROR_OBJECT(self, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }

    SnapshotManager::get_instance().take_snapshot("post_isp", input_frame_ptr);
    // the following if block should be removed once frame timestsamps are set by isp for HDR as well
    if (input_frame_ptr->isp_timestamp_ns == 0)
    {
        const auto now_time = std::chrono::system_clock::now();
        input_frame_ptr->isp_timestamp_ns =
            std::chrono::time_point_cast<std::chrono::nanoseconds>(now_time).time_since_epoch().count();
    }
    // If Denoise disbled, just push the buffer to srcpad
    if (!self->params->medialib_denoise->is_enabled())
    {
        GST_DEBUG_OBJECT(self, "Post ISP Denoise disabled, pushing buffer to srcpad");
        gst_hailo_denoise_push_output_frame(self, input_frame_ptr, buffer);
        return GST_FLOW_OK;
    }

    // check that caps have width and height of 4k
    GstVideoInfo *video_info;
    video_info = gst_video_info_new_from_caps(input_caps);
    if (!video_info)
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to get video info from caps");
        return GST_FLOW_ERROR;
    }
    gst_video_info_free(video_info);

    HailoMediaLibraryBufferPtr output_frame_ptr = std::make_shared<hailo_media_library_buffer>();

    GST_DEBUG_OBJECT(self, "Call media library handle frame - GstBuffer offset %ld", GST_BUFFER_OFFSET(buffer));
    media_library_return media_lib_ret =
        self->params->medialib_denoise->handle_frame(input_frame_ptr, output_frame_ptr);
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        if (media_lib_ret == MEDIA_LIBRARY_UNINITIALIZED)
        {
            GST_DEBUG_OBJECT(self, "Post ISP Denoise disabled, pushing buffer to srcpad");
            gst_hailo_denoise_push_output_frame(self, input_frame_ptr, buffer);
            return GST_FLOW_OK;
        }

        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        return GST_FLOW_ERROR;
    }
    if (output_frame_ptr->buffer_data == nullptr)
    {
        GST_DEBUG_OBJECT(self, "Post ISP Denoise disabled, pushing buffer to srcpad");
        gst_hailo_denoise_push_output_frame(self, input_frame_ptr, buffer);
        return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT(self, "Handle frame done");
    gst_hailo_denoise_queue_buffer(self, buffer);

    return GST_FLOW_OK;
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
    GstBufferPtr buffer = gst_hailo_denoise_dequeue_buffer(self);

    if (!buffer)
    {
        if (!self->params->m_flushing)
            GST_ERROR_OBJECT(self, "Failed to dequeue buffer");
        return;
    }

    gst_hailo_denoise_push_output_frame(self, hailo_buffer, buffer);
}

static gboolean gst_hailo_denoise_sink_event(GstPad *pad, GstObject *parent, GstEvent *gst_event)
{
    GstEventPtr event = gst_event;
    GstHailoDenoise *self = GST_HAILO_DENOISE(parent);

    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_CAPS: {
        GstCapsPtr caps_after_denoise = gst_hailo_denoise_create_srcpad_caps(self);
        return gst_pad_set_caps(self->params->srcpad, caps_after_denoise);
    }
    default:
        return glib_cpp::ptrs::pad_event_default(pad, parent, event);
    }
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

static void gst_hailo_denoise_queue_buffer(GstHailoDenoise *self, GstBufferPtr &buffer)
{
    std::unique_lock<std::mutex> lock(self->params->m_mutex);
    self->params->m_condvar.wait(lock,
                                 [self] { return self->params->m_staging_queue.size() < self->params->m_queue_size; });
    self->params->m_staging_queue.push(std::move(buffer));
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("staging queue", self->params->m_staging_queue.size(), DENOISE_TRACK);
    self->params->m_condvar.notify_one();
}

static GstBufferPtr gst_hailo_denoise_dequeue_buffer(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(self->params->m_mutex);
    self->params->m_condvar.wait(lock,
                                 [self] { return !self->params->m_staging_queue.empty() || self->params->m_flushing; });
    if (self->params->m_staging_queue.empty())
    {
        return nullptr;
    }
    GstBufferPtr buffer = std::move(self->params->m_staging_queue.front());
    self->params->m_staging_queue.pop();
    HAILO_MEDIA_LIBRARY_TRACE_COUNTER("staging queue", self->params->m_staging_queue.size(), DENOISE_TRACK);
    self->params->m_condvar.notify_one();
    return buffer;
}

static void gst_hailo_denoise_clear_staging_queue(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(self->params->m_mutex);
    while (!self->params->m_staging_queue.empty())
    {
        GstBufferPtr buffer = std::move(self->params->m_staging_queue.front());
        self->params->m_staging_queue.pop();
    }
    self->params->m_condvar.notify_one();
}
