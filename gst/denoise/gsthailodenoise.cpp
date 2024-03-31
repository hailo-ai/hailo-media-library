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
#include "gsthailodenoise.hpp"
#include "common/gstmedialibcommon.hpp"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <map>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <ctime>

GST_DEBUG_CATEGORY_STATIC(gst_hailodenoise_debug_category);
#define GST_CAT_DEFAULT gst_hailodenoise_debug_category

static void gst_hailodenoise_set_property(GObject *object,
                                          guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailodenoise_get_property(GObject *object,
                                          guint property_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_hailodenoise_change_state(GstElement *element, GstStateChange transition);
static void gst_hailodenoise_init_ghost_sink(GstHailoDenoise *hailodenoise);
static void gst_hailodenoise_init_ghost_src(GstHailoDenoise *hailodenoise);
static GstPadProbeReturn gst_hailodenoise_sink_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDenoise *hailodenoise);
static GstPadProbeReturn gst_hailodenoise_src_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDenoise *hailodenoise);
static gboolean gst_hailodenoise_create(GstHailoDenoise *self);
static void gst_hailodenoise_configure_hailonet(GstHailoDenoise *self);
static void gst_hailodenoise_configure_capsfilter(GstHailoDenoise *self);
static void gst_hailodenoise_release_hailonet(GstHailoDenoise *self);
static void gst_hailodenoise_release_capsfilter(GstHailoDenoise *self);
static void gst_hailodenoise_payload_tensor_meta(GstBuffer *buffer, GstBuffer *payload, std::string layer_name, hailo_format_order_t format_order);
static std::map<std::string, GstBuffer *> gst_hailodenoise_get_tensor_meta_from_buffer(GstBuffer *buffer);
static void gst_hailodenoise_queue_buffer(GstHailoDenoise *self, GstBuffer *buffer);
static GstBuffer *gst_hailodenoise_dequeue_buffer(GstHailoDenoise *self);
static gboolean gst_hailodenoise_remove_tensor_meta(GstBuffer *buffer);
static gboolean gst_hailodenoise_remove_tensors(GstBuffer *buffer);
static void gst_hailodenoise_clear_loopback_queue(GstHailoDenoise *self);
static gboolean gst_hailodenoise_erase_tensors(GstBuffer *buffer);

enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoDenoise, gst_hailodenoise, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailodenoise_debug_category, "hailodenoise", 0,
                                                "debug category for hailodenoise element"));

static void
gst_hailodenoise_class_init(GstHailoDenoiseClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class,
                                          "low light enhancement", "Hailo/Media-Library", "Denoising element for low light enhancement.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailodenoise_set_property;
    gobject_class->get_property = gst_hailodenoise_get_property;

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

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailodenoise_change_state);
}

static void
gst_hailodenoise_init(GstHailoDenoise *hailodenoise)
{
    // Default values
    hailodenoise->config_file_path = NULL;
    hailodenoise->medialib_denoise = NULL;
    hailodenoise->m_configured = FALSE;
    hailodenoise->m_elements_linked = FALSE;
    hailodenoise->m_queue_size = 5;
    hailodenoise->m_loop_counter = 0;
    hailodenoise->m_mutex = std::make_shared<std::mutex>();
    hailodenoise->m_condvar = std::make_unique<std::condition_variable>();
    hailodenoise->m_loopback_queue = std::queue<GstBuffer *>();
    hailodenoise->m_flushing = false;
    gst_hailodenoise_create(hailodenoise);

    // hailonet to perform the denoising
    hailodenoise->m_hailonet = NULL;
    // capsfilter to set the input format
    hailodenoise->m_capsfilter = NULL;
}

void gst_hailodenoise_set_property(GObject *object, guint property_id,
                                   const GValue *value, GParamSpec *pspec)
{
    GstHailoDenoise *hailodenoise = GST_HAILO_DENOISE(object);

    GST_DEBUG_OBJECT(hailodenoise, "set_property");
    GstState state, pending;
    GstStateChangeReturn ret = gst_element_get_state(GST_ELEMENT(hailodenoise), &state, &pending, 0);
    if (ret != GST_STATE_CHANGE_SUCCESS)
    {
        GST_ERROR_OBJECT(hailodenoise, "Failed to get state");
        return;
    }
    if (state != GST_STATE_NULL)
    {
        GST_WARNING_OBJECT(hailodenoise, "Cannot set properties while the element is in non-NULL state, please set properties before playing the pipeline");
        return;
    }
    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        hailodenoise->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(hailodenoise, "config_file_path: %s", hailodenoise->config_file_path);
        hailodenoise->config_string = gstmedialibcommon::read_json_string_from_file(hailodenoise->config_file_path);

        media_library_return config_status = hailodenoise->medialib_denoise->configure(hailodenoise->config_string);
        if (config_status != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(hailodenoise, "configuration error: %d", config_status);
            return;
        }
        break;
    }
    case PROP_CONFIG_STRING:
    {
        hailodenoise->config_string = std::string(g_value_get_string(value));
        gstmedialibcommon::strip_string_syntax(hailodenoise->config_string);

        media_library_return config_status = hailodenoise->medialib_denoise->configure(hailodenoise->config_string);
        if (config_status != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(hailodenoise, "configuration error: %d", config_status);
            return;
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        return;
    }

    if (hailodenoise->medialib_denoise->is_enabled())
    {
        gst_hailodenoise_release_capsfilter(hailodenoise);
        gst_hailodenoise_configure_hailonet(hailodenoise);
    }
    else
    {
        gst_hailodenoise_release_hailonet(hailodenoise);
        gst_hailodenoise_configure_capsfilter(hailodenoise);
    }
    gst_hailodenoise_init_ghost_sink(hailodenoise);
    gst_hailodenoise_init_ghost_src(hailodenoise);

    hailodenoise->m_configured = TRUE;

    // Now that configuration is known, link the elements
    if (hailodenoise->m_elements_linked == FALSE)
    {
        hailodenoise->m_elements_linked = TRUE;
    }
}

void gst_hailodenoise_get_property(GObject *object, guint property_id,
                                   GValue *value, GParamSpec *pspec)
{
    GstHailoDenoise *hailodenoise = GST_HAILO_DENOISE(object);

    GST_DEBUG_OBJECT(hailodenoise, "get_property");

    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, hailodenoise->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailodenoise->config_string.c_str());
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean
gst_hailodenoise_create(GstHailoDenoise *self)
{
    tl::expected<MediaLibraryDenoisePtr, media_library_return> denoise = MediaLibraryDenoise::create();
    if (denoise.has_value())
    {
        self->medialib_denoise = denoise.value();
    }
    else
    {
        GST_ERROR_OBJECT(self, "Denoise configuration error: %d", denoise.error());
        throw std::runtime_error("Denoise failed to configure, check config file.");
    }
    return TRUE;
}

static void
gst_hailodenoise_configure_capsfilter(GstHailoDenoise *self)
{

    if (!self->m_capsfilter)
    {
        self->m_capsfilter = gst_element_factory_make("capsfilter", NULL);
        // Connect elements and pads in the bin
        gst_bin_add_many(GST_BIN(self), self->m_capsfilter, NULL);
        // set the m_capsfilter properties
        g_object_set(self->m_capsfilter, "caps", gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT, 3840, "height", G_TYPE_INT, 2160, "framerate", GST_TYPE_FRACTION, 30, 1, NULL), NULL);
    }
}

static void
gst_hailodenoise_release_capsfilter(GstHailoDenoise *self)
{
    if (self->m_capsfilter)
    {
        gst_element_set_state(self->m_capsfilter, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(self), self->m_capsfilter);
        gst_object_unref(self->m_capsfilter);
        self->m_capsfilter = NULL;
    }
}

static void
gst_hailodenoise_configure_hailonet(GstHailoDenoise *self)
{
    if (!self->m_hailonet)
    {
        self->m_hailonet = gst_element_factory_make("hailonet", NULL);
        // Connect elements and pads in the bin
        gst_bin_add_many(GST_BIN(self), self->m_hailonet, NULL);
    }
    // get the hailort configurations from medialib_denoise, and set m_hailonet properties using the found config
    hailort_t hailort_configs = self->medialib_denoise->get_hailort_configs();
    denoise_config_t denoise_configs = self->medialib_denoise->get_denoise_configs();
    // set the hailonet properties
    // Some HailoRT parameters cannot be changed once configured
    g_object_set(self->m_hailonet, "hef-path", denoise_configs.network_config.network_path.c_str(), NULL);
    g_object_set(self->m_hailonet, "input-from-meta", true, NULL);
    g_object_set(self->m_hailonet, "no-transform", true, NULL);
    g_object_set(self->m_hailonet, "scheduling-algorithm", 1, NULL);
    g_object_set(self->m_hailonet, "outputs-min-pool-size", 0, NULL);
    g_object_set(self->m_hailonet, "outputs-max-pool-size", denoise_configs.loopback_count+1, NULL); // hailonet will hold two internal queues of outputs-max-pool-size
    g_object_set(self->m_hailonet, "vdevice-group-id", hailort_configs.device_id.c_str(), NULL);
    g_object_set(self->m_hailonet, "pass-through", !denoise_configs.enabled, NULL);

    // reset the loopback counter for the next time we enable
    self->m_loop_counter = 0;
    // Clear the loopback queue since those buffers are not valid for the next time we enable
    gst_hailodenoise_clear_loopback_queue(self);
}

static void
gst_hailodenoise_release_hailonet(GstHailoDenoise *self)
{
    if (self->m_hailonet)
    {
        gst_element_set_state(self->m_hailonet, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(self), self->m_hailonet);
        gst_object_unref(self->m_hailonet);
        self->m_hailonet = NULL;
    }
}

static void
gst_hailodenoise_init_ghost_sink(GstHailoDenoise *hailodenoise)
{
    GstPad *pad = NULL;
    GST_DEBUG_OBJECT(hailodenoise, "Initializing ghost sink pad");
    // Get the connecting pad
    if (hailodenoise->medialib_denoise->is_enabled())
    {
        pad = gst_element_get_static_pad(hailodenoise->m_hailonet, "sink");
    }
    else
    {
        pad = gst_element_get_static_pad(hailodenoise->m_capsfilter, "sink");
    }

    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sink_template);
    hailodenoise->sinkpad = gst_ghost_pad_new_from_template("sink", pad, pad_tmpl);
    gst_pad_set_active(hailodenoise->sinkpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailodenoise), hailodenoise->sinkpad);

    if (hailodenoise->medialib_denoise->is_enabled())
    {
        // Add a probe for internal logic
        hailodenoise->sink_probe_id = gst_pad_add_probe(hailodenoise->sinkpad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER),
                                                        (GstPadProbeCallback)gst_hailodenoise_sink_probe, hailodenoise, NULL);
    }

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

static void
gst_hailodenoise_init_ghost_src(GstHailoDenoise *hailodenoise)
{
    GstPad *pad = NULL;
    GST_DEBUG_OBJECT(hailodenoise, "Initializing ghost src pad");
    // Get the connecting pad
    if (hailodenoise->medialib_denoise->is_enabled())
    {
        pad = gst_element_get_static_pad(hailodenoise->m_hailonet, "src");
    }
    else
    {
        pad = gst_element_get_static_pad(hailodenoise->m_capsfilter, "src");
    }

    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&src_template);
    hailodenoise->srcpad = gst_ghost_pad_new_from_template("src", pad, pad_tmpl);
    gst_pad_set_active(hailodenoise->srcpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailodenoise), hailodenoise->srcpad);

    if (hailodenoise->medialib_denoise->is_enabled())
    {
        // Add a probe for internal logic
        hailodenoise->src_probe_id = gst_pad_add_probe(hailodenoise->srcpad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER),
                                                       (GstPadProbeCallback)gst_hailodenoise_src_probe, hailodenoise, NULL);
    }

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

static GstStateChangeReturn
gst_hailodenoise_change_state(GstElement *element, GstStateChange transition)
{
    GstHailoDenoise *hailodenoise = GST_HAILO_DENOISE(element);
    // If transition is PAUSED to READY, remove the probes
    switch (transition)
    {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        if (hailodenoise->medialib_denoise->is_enabled())
        {
            // Set pass-through as true.
            g_object_set(hailodenoise->m_hailonet, "pass-through", true, NULL);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
        if (hailodenoise->medialib_denoise->is_enabled())
        {
            // Set flushing as true.
            hailodenoise->m_flushing = true;
            // Remove the probes
            gst_pad_remove_probe(hailodenoise->sinkpad, hailodenoise->sink_probe_id);
            gst_pad_remove_probe(hailodenoise->srcpad, hailodenoise->src_probe_id);
            // Clear the loopback queue since those buffers are not valid for the next time we enable
            gst_hailodenoise_clear_loopback_queue(hailodenoise);
        }
        break;
    }
    default:
        break;
    }

    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailodenoise_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret)
    {
        return ret;
    }
    if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    {
        // Set flushing to false again
        hailodenoise->m_flushing = false;
    }
    return ret;
}

static gboolean
gst_hailodenoise_remove_tensor_meta(GstBuffer *buffer)
{
    gpointer state = NULL;
    GstMeta *meta;
    std::vector<GstMeta *> meta_vector;
    gboolean ret = false;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_TENSOR_META_API_TYPE)))
    {
        meta_vector.emplace_back(std::move(meta));
    }
    for (auto meta : meta_vector)
    {
        ret = gst_buffer_remove_meta(buffer, meta);
        if (ret == false)
            return ret;
    }
    return true;
}

static gboolean
gst_hailodenoise_erase_tensors(GstBuffer *buffer)
{
    gpointer state = NULL;
    GstMeta *meta;
    GstParentBufferMeta *pmeta;
    std::vector<GstMeta *> meta_vector;
    gboolean ret = false;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE)))
    {
        pmeta = reinterpret_cast<GstParentBufferMeta *>(meta);
        // check if the buffer has tensor metadata
        if (!gst_buffer_get_meta(pmeta->buffer, g_type_from_name(TENSOR_META_API_NAME)))
        {
            continue;
        }
        gst_buffer_unref(pmeta->buffer);
        meta_vector.emplace_back(std::move(meta));
    }
    for (auto meta : meta_vector)
    {
        ret = gst_buffer_remove_meta(buffer, meta);
        if (ret == false)
            return ret;
    }
    return true;
}

static gboolean
gst_hailodenoise_remove_tensors(GstBuffer *buffer)
{
    gpointer state = NULL;
    GstMeta *meta;
    GstParentBufferMeta *pmeta;
    std::vector<GstMeta *> meta_vector;
    gboolean ret = false;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE)))
    {
        pmeta = reinterpret_cast<GstParentBufferMeta *>(meta);
        // check if the buffer has tensor metadata
        if (!gst_buffer_get_meta(pmeta->buffer, g_type_from_name(TENSOR_META_API_NAME)))
        {
            continue;
        }
        // gst_buffer_unref(pmeta->buffer);
        meta_vector.emplace_back(std::move(meta));
    }
    for (auto meta : meta_vector)
    {
        ret = gst_buffer_remove_meta(buffer, meta);
        if (ret == false)
            return ret;
    }
    return true;
}

static void
gst_hailodenoise_payload_tensor_meta(GstBuffer *buffer, GstBuffer *payload, std::string layer_name, hailo_format_order_t format_order)
{
    GstHailoTensorMeta *meta = GST_TENSOR_META_ADD(payload);
    if (meta == nullptr)
    {
        g_error("GstHailoDenoise: GstHailoTensorMeta is null when payloading\n");
    }
    memset(&meta->info, 0, sizeof(meta->info));
    memcpy(meta->info.name, layer_name.c_str(), layer_name.size());
    meta->info.format.order = format_order;
    gst_buffer_add_parent_buffer_meta(buffer, payload);
}

static std::map<std::string, GstBuffer *>
gst_hailodenoise_get_tensor_meta_from_buffer(GstBuffer *buffer)
{
    std::map<std::string, GstBuffer *> meta_vector;

    gpointer state = NULL;
    GstMeta *meta;
    GstParentBufferMeta *pmeta;
    GstMapInfo info;
    if (buffer == nullptr)
    {
        g_error("gst_hailodenoise_get_tensor_meta_from_buffer: Buffer is null\n");
        return meta_vector;
    }
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_PARENT_BUFFER_META_API_TYPE)))
    {
        pmeta = reinterpret_cast<GstParentBufferMeta *>(meta);
        (void)gst_buffer_map(pmeta->buffer, &info, GstMapFlags(GST_VIDEO_FRAME_MAP_FLAG_NO_REF));
        // check if the buffer has tensor metadata
        if (!gst_buffer_get_meta(pmeta->buffer, g_type_from_name(TENSOR_META_API_NAME)))
        {
            gst_buffer_unmap(pmeta->buffer, &info);
            continue;
        }
        const hailo_vstream_info_t vstream_info = reinterpret_cast<GstHailoTensorMeta *>(gst_buffer_get_meta(pmeta->buffer, g_type_from_name(TENSOR_META_API_NAME)))->info;
        meta_vector[vstream_info.name] = pmeta->buffer;
        gst_buffer_unmap(pmeta->buffer, &info);
    }

    return meta_vector;
}

static GstPadProbeReturn
gst_hailodenoise_sink_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDenoise *hailodenoise)
{
    // Handle incoming data
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER))
    {
        // probe recieved something other than a buffer, pass it immediately
        return GST_PAD_PROBE_PASS;
    }

    // Get the buffer
    GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
    if (buffer == nullptr)
    {
        GST_ERROR_OBJECT(hailodenoise, "Buffer is null at sink probe");
        return GST_PAD_PROBE_DROP;
    }
    if (!gst_buffer_is_writable(buffer))
    {
        GST_WARNING_OBJECT(hailodenoise, "Buffer is not writable at sink probe");
    }

    // Get the network configurations
    feedback_network_config_t net_configs = hailodenoise->medialib_denoise->get_denoise_configs().network_config;
    uint loopback = hailodenoise->medialib_denoise->get_denoise_configs().loopback_count;

    // Retrieve the plane data from the incoming buffer
    GstVideoFrame frame;
    GstVideoInfo video_info;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    gst_video_info_from_caps(&video_info, caps);
    gst_caps_unref(caps);
    gst_video_frame_map(&frame, &video_info, buffer, GST_MAP_READ);
    // retrieve the y and uv channels
    uint8_t *y_channel = reinterpret_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    uint8_t *uv_channel = reinterpret_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 1));
    gsize y_channel_size = GST_VIDEO_FRAME_COMP_STRIDE(&frame, 0) * GST_VIDEO_FRAME_HEIGHT(&frame);
    gsize uv_channel_size = GST_VIDEO_FRAME_COMP_STRIDE(&frame, 1) * GST_VIDEO_FRAME_HEIGHT(&frame) / 2;
    gst_video_frame_unmap(&frame);

    // Y
    GstBuffer *y_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, y_channel, y_channel_size, 0, y_channel_size, NULL, NULL);
    gst_hailodenoise_payload_tensor_meta(buffer, y_buffer, net_configs.y_channel, HAILO_FORMAT_ORDER_NHCW);

    // UV
    GstBuffer *uv_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, uv_channel, uv_channel_size, 0, uv_channel_size, NULL, NULL);
    gst_hailodenoise_payload_tensor_meta(buffer, uv_buffer, net_configs.uv_channel, HAILO_FORMAT_ORDER_NHWC);

    if (hailodenoise->m_loop_counter < loopback || !hailodenoise->medialib_denoise->is_enabled())
    {
        // Y Feedback
        GstBuffer *feedback_y_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, y_channel, y_channel_size, 0, y_channel_size, NULL, NULL);
        gst_hailodenoise_payload_tensor_meta(buffer, feedback_y_buffer, net_configs.feedback_y_channel, HAILO_FORMAT_ORDER_NHCW);

        // UV Feedback
        GstBuffer *feedback_uv_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, uv_channel, uv_channel_size, 0, uv_channel_size, NULL, NULL);
        gst_hailodenoise_payload_tensor_meta(buffer, feedback_uv_buffer, net_configs.feedback_uv_channel, HAILO_FORMAT_ORDER_NHWC);

        // Increment the loop counter
        hailodenoise->m_loop_counter++;
    }
    else
    {
        // Get the next buffer from the loopback queue
        GstBuffer *loopback_buffer = gst_hailodenoise_dequeue_buffer(hailodenoise);
        if (!loopback_buffer)
        {
            if (hailodenoise->m_flushing)
            {
                GST_INFO_OBJECT(hailodenoise, "Flushing, drop frame, do not loop-back");
            }
            else
            {
                GST_ERROR_OBJECT(hailodenoise, "Loopback buffer is null");
            }
            return GST_PAD_PROBE_REMOVE;
        }

        // Retrieve the plane data from the loopback buffer
        std::map<std::string, GstBuffer *> loopback_tensors = gst_hailodenoise_get_tensor_meta_from_buffer(loopback_buffer);

        // Y
        GstBuffer *y_tensor_buffer = loopback_tensors[net_configs.output_y_channel];
        (void)gst_hailodenoise_remove_tensor_meta(y_tensor_buffer); // Clean out the old tensor meta
        gst_hailodenoise_payload_tensor_meta(buffer, y_tensor_buffer, net_configs.feedback_y_channel, HAILO_FORMAT_ORDER_NHCW);

        // UV
        GstBuffer *uv_tensor_buffer = loopback_tensors[net_configs.output_uv_channel];
        (void)gst_hailodenoise_remove_tensor_meta(uv_tensor_buffer); // Clean out the old tensor meta
        gst_hailodenoise_payload_tensor_meta(buffer, uv_tensor_buffer, net_configs.feedback_uv_channel, HAILO_FORMAT_ORDER_NHWC);

        // Unref the loopback buffer
        (void)gst_hailodenoise_remove_tensors(loopback_buffer);
        gst_buffer_unref(loopback_buffer);
    }

    return GST_PAD_PROBE_PASS;
}

static GstPadProbeReturn
gst_hailodenoise_src_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDenoise *hailodenoise)
{
    // Handle outgoing data
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER))
    {
        // probe recieved something other than a buffer, pass it immediately
        return GST_PAD_PROBE_PASS;
    }

    // Get the buffer
    GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
    if (buffer == nullptr)
    {
        GST_ERROR_OBJECT(hailodenoise, "Buffer is null at src probe");
        return GST_PAD_PROBE_DROP;
    }
    if (!gst_buffer_is_writable(buffer))
    {
        GST_WARNING_OBJECT(hailodenoise, "Buffer is not writable at src probe");
    }
    if (!hailodenoise->medialib_denoise->is_enabled())
    {
        // Denoising is disabled, remove any input tensors and pass
        (void)gst_hailodenoise_erase_tensors(buffer);
        return GST_PAD_PROBE_PASS;
    }

    // Get the network configurations
    feedback_network_config_t net_configs = hailodenoise->medialib_denoise->get_denoise_configs().network_config;

    // Retrieve the tensor data from the incoming buffer
    std::map<std::string, GstBuffer *> output_tensors = gst_hailodenoise_get_tensor_meta_from_buffer(buffer);
    // Y
    GstBuffer *y_tensor_buffer = output_tensors[net_configs.output_y_channel];
    // UV
    GstBuffer *uv_tensor_buffer = output_tensors[net_configs.output_uv_channel];

    if (y_tensor_buffer == nullptr || uv_tensor_buffer == nullptr)
    {
        GST_INFO_OBJECT(hailodenoise, "We are in closing/flushing stage. Drop frame, do not loop-back");
        return GST_PAD_PROBE_DROP;
    }
    // Get the planes to replace in the incoming buffer
    GstVideoFrame frame;
    GstVideoInfo video_info;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    gst_video_info_from_caps(&video_info, caps);
    gst_caps_unref(caps);
    gst_video_frame_map(&frame, &video_info, buffer, GstMapFlags(GST_VIDEO_FRAME_MAP_FLAG_NO_REF));
    uint8_t *y_channel = reinterpret_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    uint8_t *uv_channel = reinterpret_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 1));
    gst_video_frame_unmap(&frame);

    // Copy the tensor data into the planes
    // Y
    GstMapInfo y_info;
    (void)gst_buffer_map(y_tensor_buffer, &y_info, GstMapFlags(GST_VIDEO_FRAME_MAP_FLAG_NO_REF));
    uint8_t *y_tensor_data = reinterpret_cast<uint8_t *>(y_info.data);
    memcpy(y_channel, y_tensor_data, y_info.size);
    gst_buffer_unmap(y_tensor_buffer, &y_info);

    // UV
    GstMapInfo uv_info;
    (void)gst_buffer_map(uv_tensor_buffer, &uv_info, GstMapFlags(GST_VIDEO_FRAME_MAP_FLAG_NO_REF));
    uint8_t *uv_tensor_data = reinterpret_cast<uint8_t *>(uv_info.data);
    memcpy(uv_channel, uv_tensor_data, uv_info.size);
    gst_buffer_unmap(uv_tensor_buffer, &uv_info);

    // create a new empty gstbuffer and payload the tensors to it
    GstBuffer *loopback_payload = gst_buffer_new();
    gst_buffer_add_parent_buffer_meta(loopback_payload, y_tensor_buffer);
    gst_buffer_add_parent_buffer_meta(loopback_payload, uv_tensor_buffer);

    // Copy is done, remove the tensors from the buffer
    (void)gst_hailodenoise_remove_tensors(buffer);

    // Loop-back the denoised buffer
    gst_hailodenoise_queue_buffer(hailodenoise, loopback_payload);

    return GST_PAD_PROBE_PASS;
}

static void
gst_hailodenoise_queue_buffer(GstHailoDenoise *self, GstBuffer *buffer)
{
    std::unique_lock<std::mutex> lock(*(self->m_mutex));
    self->m_condvar->wait(lock, [self]
                          { return self->m_loopback_queue.size() < self->m_queue_size; });
    self->m_loopback_queue.push(buffer);
    self->m_condvar->notify_one();
}

static GstBuffer *
gst_hailodenoise_dequeue_buffer(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(*(self->m_mutex));
    self->m_condvar->wait(lock, [self]
                          { return !self->m_loopback_queue.empty() || self->m_flushing; });
    if (self->m_loopback_queue.empty())
    {
        return nullptr;
    }
    GstBuffer *buffer = self->m_loopback_queue.front();
    self->m_loopback_queue.pop();
    self->m_condvar->notify_one();
    return buffer;
}

static void
gst_hailodenoise_clear_loopback_queue(GstHailoDenoise *self)
{
    std::unique_lock<std::mutex> lock(*(self->m_mutex));
    while (!self->m_loopback_queue.empty())
    {
        GstBuffer *buffer = self->m_loopback_queue.front();
        self->m_loopback_queue.pop();
        gst_buffer_unref(buffer);
    }
    self->m_condvar->notify_one();
}
