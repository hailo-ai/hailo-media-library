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
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include <gst/video/video.h>
#include <tl/expected.hpp>

GST_DEBUG_CATEGORY_STATIC(gst_hailo_denoise_debug);
#define GST_CAT_DEFAULT gst_hailo_denoise_debug

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
    GST_DEBUG_CATEGORY_INIT(gst_hailo_denoise_debug, "hailodenoise", 0, "Hailo low light enhancement element");

#define gst_hailo_denoise_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoDenoise, gst_hailo_denoise, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_denoise_finalize(GObject *object);
static void gst_hailo_denoise_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_denoise_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_hailo_denoise_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean gst_hailo_denoise_create(GstHailoDenoise *self);
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

static void
gst_hailo_denoise_class_init(GstHailoDenoiseClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    // Pad templates
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    // Set metadata
    gst_element_class_set_static_metadata(element_class,
                                          "low light enhancement", "Hailo/Media-Library", "Denoising element for low light enhancement.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailo_denoise_set_property;
    gobject_class->get_property = gst_hailo_denoise_get_property;
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_hailo_denoise_finalize);

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
    
    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_pointer("config", "Denoise config", "Fronted config as denoise_config_t",
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

}

static void
gst_hailo_denoise_init(GstHailoDenoise *denoise)
{
    GST_DEBUG_OBJECT(denoise, "init");
    denoise->config_file_path = NULL;
    denoise->medialib_denoise = NULL;
    denoise->m_flushing = false;
    denoise->m_mutex = std::make_shared<std::mutex>();
    denoise->m_config_mutex = std::make_shared<std::mutex>();
    denoise->m_condvar = std::make_unique<std::condition_variable>();
    denoise->m_queue_size = 2;
    denoise->m_staging_queue = std::queue<GstBuffer *>();

    denoise->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    denoise->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(denoise->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_chain));
    gst_pad_set_query_function(denoise->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_denoise_sink_query));

    GST_PAD_SET_PROXY_CAPS(denoise->sinkpad);
    gst_element_add_pad(GST_ELEMENT(denoise), denoise->sinkpad);
    gst_element_add_pad(GST_ELEMENT(denoise), denoise->srcpad);
}

static void gst_hailo_denoise_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(object);
    std::unique_lock<std::mutex> lock(*(self->m_config_mutex));

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        self->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "config_file_path: %s", self->config_file_path);
        self->config_string = gstmedialibcommon::read_json_string_from_file(self->config_file_path);

        if (self->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self);
        }
        else
        {
            media_library_return config_status = self->medialib_denoise->configure(self->config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        self->config_string = g_strdup(g_value_get_string(value));
        gstmedialibcommon::strip_string_syntax(self->config_string);

        if (self->medialib_denoise == nullptr)
        {
            gst_hailo_denoise_create(self);
        }
        else
        {
            media_library_return config_status = self->medialib_denoise->configure(self->config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(self, "configuration error: %d", config_status);
        }
        break;
    }
    case PROP_CONFIG:
    {   
        if(self->medialib_denoise)
        {
            denoise_config_t *denoise_config = static_cast<denoise_config_t *>(g_value_get_pointer(value));
            hailort_t hailort_configs = self->medialib_denoise->get_hailort_configs();

            if(self->medialib_denoise->configure(*denoise_config, hailort_configs) != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to configure dewarp with denoise_config_t object");
            }
            else
            {
                self->denoise_config = std::make_shared<denoise_config_t>(*denoise_config);
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
gst_hailo_denoise_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(object);

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
    case PROP_CONFIG:
    {
        self->denoise_config = std::make_shared<denoise_config_t>(self->medialib_denoise->get_denoise_configs());
        g_value_set_pointer(value, self->denoise_config.get());
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean
gst_hailo_denoise_create(GstHailoDenoise *self)
{
    tl::expected<MediaLibraryDenoisePtr, media_library_return> denoise = MediaLibraryDenoise::create(self->config_string);
    if (denoise.has_value())
    {
        self->medialib_denoise = denoise.value();
    }
    else
    {
        GST_ERROR_OBJECT(self, "Denoise configuration error: %d", denoise.error());
        throw std::runtime_error("Denoise failed to configure, check config file.");
    }

    // set event callbacks
    MediaLibraryDenoise::callbacks_t callbacks;
    callbacks.on_buffer_ready = [self](HailoMediaLibraryBufferPtr out_buf)
    {
        gst_hailo_denoise_deploy_buffer(self, out_buf);
    };
    callbacks.on_enable_changed = [self](bool enabled)
    {
        if (!enabled)
        {
            self->m_flushing = true;
            self->m_condvar->notify_one();
            gst_hailo_denoise_clear_staging_queue(self);
        }
    };
    self->medialib_denoise->observe(callbacks);

    return TRUE;
}

static GstFlowReturn
gst_hailo_denoise_push_output_frame(GstHailoDenoise *self,
                                    HailoMediaLibraryBufferPtr hailo_buffer,
                                    GstBuffer *buffer)
{
    GstFlowReturn ret = GST_FLOW_OK;

    if (hailo_buffer->hailo_pix_buffer == nullptr)
    {
        GST_ERROR_OBJECT(self, "Trying to push null output frame");
        ret = GST_FLOW_ERROR;
        return ret;
    }

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
        GST_ERROR_OBJECT(self, "Failed to create GstBuffer from media library buffer");
        hailo_buffer->decrease_ref_count();
        ret = GST_FLOW_ERROR;
        return ret;
    }

    GST_DEBUG_OBJECT(self, "Pushing buffer to srcpad name %s", gst_pad_get_name(self->srcpad));
    gst_outbuf->pts = GST_BUFFER_PTS(buffer);
    gst_outbuf->offset = GST_BUFFER_OFFSET(buffer);
    gst_outbuf->duration = GST_BUFFER_DURATION(buffer);
    // add vsm meta?
    gst_pad_push(self->srcpad, gst_outbuf);

    return ret;
}

static GstFlowReturn
gst_hailo_denoise_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(parent);
    GstFlowReturn ret = GST_FLOW_OK;
    std::unique_lock<std::mutex> lock(*(self->m_config_mutex));

    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad");

    // If Denoise disbled, just push the buffer to srcpad
    if (!self->medialib_denoise->is_enabled())
    {
        GST_DEBUG_OBJECT(self, "Denoise disabled, pushing buffer to srcpad");
        gst_pad_push(self->srcpad, buffer);
        return ret;
    }

    GstCaps *input_caps = gst_pad_get_current_caps(pad);

    // check that caps have width and height of 4k
    GstVideoInfo *video_info;
    video_info = gst_video_info_new_from_caps(input_caps);
    if (!video_info)
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Failed to get video info from caps");
        return GST_FLOW_ERROR;
    } else if (video_info-> width != 3840 || video_info->height != 2160)
    {
        GST_CAT_ERROR(GST_CAT_DEFAULT, "Denoising currently only supported in 4k, check CAPS.");
        gst_video_info_free(video_info);
        return GST_FLOW_ERROR;
    }
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
    media_library_return media_lib_ret = self->medialib_denoise->handle_frame(input_frame_ptr, output_frame_ptr);
    input_frame_ptr->decrease_ref_count(); //decrease ref count regardless of success
    if (media_lib_ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Media library handle frame failed on error %d", media_lib_ret);
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    lock.unlock();

    GST_DEBUG_OBJECT(self, "Handle frame done");
    gst_hailo_denoise_queue_buffer(self, buffer);

    return ret;
}

static void
gst_hailo_denoise_deploy_buffer(GstHailoDenoise *self, HailoMediaLibraryBufferPtr hailo_buffer)
{
    GstBuffer *buffer = gst_hailo_denoise_dequeue_buffer(self);
    if (!buffer)
    {
        if (!self->m_flushing)
            GST_ERROR_OBJECT(self, "Failed to dequeue buffer");
        return;
    }

    gst_hailo_denoise_push_output_frame(self, hailo_buffer, buffer);
    gst_buffer_unref(buffer);
}

static gboolean
gst_hailo_denoise_sink_query(GstPad *pad,
                            GstObject *parent, GstQuery *query)
{
    GstHailoDenoise *self = GST_HAILO_DENOISE(parent);
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
    default:
    {
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
    if (self->config_file_path)
    {
        g_free(self->config_file_path);
        self->config_file_path = NULL;
    }


    self->m_flushing = true;
    gst_hailo_denoise_clear_staging_queue(self);
    if (self->medialib_denoise)
    {
        self->medialib_denoise.reset();
        self->medialib_denoise = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_hailo_denoise_queue_buffer(GstHailoDenoise *self, GstBuffer *buffer)
{
    std::unique_lock<std::mutex> lock(*(self->m_mutex));
    self->m_condvar->wait(lock, [self]
                          { return self->m_staging_queue.size() < self->m_queue_size; });
    self->m_staging_queue.push(buffer);
    self->m_condvar->notify_one();
}

static GstBuffer *
gst_hailo_denoise_dequeue_buffer(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(*(self->m_mutex));
    self->m_condvar->wait(lock, [self]
                          { return !self->m_staging_queue.empty() || self->m_flushing; });
    if (self->m_staging_queue.empty())
    {
        return nullptr;
    }
    GstBuffer *buffer = self->m_staging_queue.front();
    self->m_staging_queue.pop();
    self->m_condvar->notify_one();
    return buffer;
}

static void
gst_hailo_denoise_clear_staging_queue(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(*(self->m_mutex));
    while (!self->m_staging_queue.empty())
    {
        GstBuffer *buffer = self->m_staging_queue.front();
        self->m_staging_queue.pop();
        gst_buffer_unref(buffer);
    }
    self->m_condvar->notify_one();
}
