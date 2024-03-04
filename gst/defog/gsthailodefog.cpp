/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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
#include "gsthailodefog.hpp"
#include "common/gstmedialibcommon.hpp"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <map>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <ctime>

GST_DEBUG_CATEGORY_STATIC(gst_hailodefog_debug_category);
#define GST_CAT_DEFAULT gst_hailodefog_debug_category

#define ROTATION_EVENT_NAME "HAILO_ROTATION_EVENT"
#define ROTATION_EVENT_PROP_NAME "rotation"

static void gst_hailodefog_set_property(GObject *object,
                                          guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailodefog_get_property(GObject *object,
                                          guint property_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_hailodefog_change_state(GstElement *element, GstStateChange transition);
static void gst_hailodefog_init_ghost_sink(GstHailoDefog *hailodefog);
static void gst_hailodefog_init_ghost_src(GstHailoDefog *hailodefog);
static GstPadProbeReturn gst_hailodefog_sink_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDefog *hailodefog);
static GstPadProbeReturn gst_hailodefog_src_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDefog *hailodefog);
static gboolean gst_hailodefog_create(GstHailoDefog *self);
static gboolean gst_hailodefog_configure_hailonet(GstHailoDefog *self);
static void gst_hailodefog_payload_tensor_meta(GstBuffer *buffer, GstBuffer *payload, std::string layer_name, hailo_format_order_t format_order);
static std::map<std::string, GstBuffer*> gst_hailodefog_get_tensor_meta_from_buffer(GstBuffer *buffer);
static gboolean gst_hailodefog_remove_tensors(GstBuffer *buffer);
static gboolean gst_hailodefog_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_hailodefog_erase_tensors(GstBuffer *buffer);

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

G_DEFINE_TYPE_WITH_CODE(GstHailoDefog, gst_hailodefog, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailodefog_debug_category, "hailodefog", 0,
                                                "debug category for hailodefog element"));

static void
gst_hailodefog_class_init(GstHailoDefogClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class,
                                          "de-fogging enhancement", "Hailo/Media-Library", "Dehazing element for de-fogging enhancement.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailodefog_set_property;
    gobject_class->get_property = gst_hailodefog_get_property;

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

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailodefog_change_state);

}

static void
gst_hailodefog_init(GstHailoDefog *hailodefog)
{
    // Default values
    hailodefog->config_file_path = NULL;
    hailodefog->medialib_defog = NULL;
    hailodefog->m_elements_linked = FALSE;
    hailodefog->m_configured = FALSE;
    hailodefog->m_rotated = FALSE;

    // Prepare internal elements

    // hailonet2 to perform the defog
    hailodefog->m_hailonet = gst_element_factory_make("hailonet2", NULL);

    // Connect elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailodefog), hailodefog->m_hailonet, NULL);
    gst_hailodefog_init_ghost_sink(hailodefog);
    gst_hailodefog_init_ghost_src(hailodefog);
}

void gst_hailodefog_set_property(GObject *object, guint property_id,
                                   const GValue *value, GParamSpec *pspec)
{
    GstHailoDefog *hailodefog = GST_HAILO_DEFOG(object);

    GST_DEBUG_OBJECT(hailodefog, "set_property");

    switch (property_id)
    {
    // Handle property assignments here
    case PROP_CONFIG_FILE_PATH:
    {
        hailodefog->config_file_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(hailodefog, "config_file_path: %s", hailodefog->config_file_path);
        hailodefog->config_string = gstmedialibcommon::read_json_string_from_file(hailodefog->config_file_path);

        if (hailodefog->medialib_defog == nullptr)
        {
            gst_hailodefog_create(hailodefog);
        }
        else
        {
            media_library_return config_status = hailodefog->medialib_defog->configure(hailodefog->config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(hailodefog, "configuration error: %d", config_status);
        }
        gst_hailodefog_configure_hailonet(hailodefog);
        hailodefog->m_configured = TRUE;
        break;
    }
    case PROP_CONFIG_STRING:
    {
        hailodefog->config_string = g_strdup(g_value_get_string(value));
        gstmedialibcommon::strip_string_syntax(hailodefog->config_string);

        if (hailodefog->medialib_defog == nullptr)
        {
            gst_hailodefog_create(hailodefog);
        }
        else
        {
            media_library_return config_status = hailodefog->medialib_defog->configure(hailodefog->config_string);
            if (config_status != MEDIA_LIBRARY_SUCCESS)
                GST_ERROR_OBJECT(hailodefog, "configuration error: %d", config_status);
        }
        gst_hailodefog_configure_hailonet(hailodefog);
        hailodefog->m_configured = TRUE;
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailodefog_get_property(GObject *object, guint property_id,
                                   GValue *value, GParamSpec *pspec)
{
    GstHailoDefog *hailodefog = GST_HAILO_DEFOG(object);

    GST_DEBUG_OBJECT(hailodefog, "get_property");

    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, hailodefog->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailodefog->config_string.c_str());
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean
gst_hailodefog_create(GstHailoDefog *self)
{
  tl::expected<MediaLibraryDefogPtr, media_library_return> defog = MediaLibraryDefog::create(self->config_string);
  if (defog.has_value())
  {
    self->medialib_defog = defog.value();
  }
  else
  {
    GST_ERROR_OBJECT(self, "Defog configuration error: %d", defog.error());
    throw std::runtime_error("Defog failed to configure, check config file.");
  }
  return TRUE;
}

static gboolean
gst_hailodefog_configure_hailonet(GstHailoDefog *self)
{
    // get the hailort configurations from medialib_defog, and set m_hailonet properties using the found config
    hailort_t hailort_configs = self->medialib_defog->get_hailort_configs();
    defog_config_t defog_configs = self->medialib_defog->get_defog_configs();
    // set the hailonet properties
    if (!self->m_configured)
    {
        // Some HailoRT parameters cannot be changed once configured
        g_object_set(self->m_hailonet, "hef-path", defog_configs.network_config.network_path.c_str(), NULL);
        g_object_set(self->m_hailonet, "input-from-meta", true, NULL);
        g_object_set(self->m_hailonet, "no-transform", true, NULL);
        g_object_set(self->m_hailonet, "scheduling-algorithm", 1, NULL);
        g_object_set(self->m_hailonet, "outputs-min-pool-size", 0, NULL);
        g_object_set(self->m_hailonet, "outputs-max-pool-size", 2, NULL);
        g_object_set(self->m_hailonet, "vdevice-group-id", hailort_configs.device_id.c_str(), NULL);
    }
    if (self->m_rotated)
    {
        // Rotation is not supported, disable defog
        g_object_set(self->m_hailonet, "pass-through", true, NULL);
        return TRUE;
    } else {
        g_object_set(self->m_hailonet, "pass-through", !defog_configs.enabled, NULL);
    }
    return TRUE;
}

static void 
gst_hailodefog_init_ghost_sink(GstHailoDefog *hailodefog)
{
    // Get the connecting pad
    GstPad *pad = gst_element_get_static_pad(hailodefog->m_hailonet, "sink");

    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sink_template);
    hailodefog->sinkpad = gst_ghost_pad_new_from_template("sink", pad, pad_tmpl);
    gst_pad_set_active(hailodefog->sinkpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailodefog), hailodefog->sinkpad);

    // Add a probe for internal logic
    gst_pad_add_probe(hailodefog->sinkpad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER),
                      (GstPadProbeCallback)gst_hailodefog_sink_probe, hailodefog, NULL);

    // Add event handling
    gst_pad_set_event_function(hailodefog->sinkpad, GST_DEBUG_FUNCPTR(gst_hailodefog_sink_event));

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

static void
gst_hailodefog_init_ghost_src(GstHailoDefog *hailodefog)
{
    // Get the connecting pad
    GstPad *pad = gst_element_get_static_pad(hailodefog->m_hailonet, "src");

    // Create a ghostpad and connect it to the bin
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&src_template);
    hailodefog->srcpad = gst_ghost_pad_new_from_template("src", pad, pad_tmpl);
    gst_pad_set_active(hailodefog->srcpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(hailodefog), hailodefog->srcpad);

    // Add a probe for internal logic
    gst_pad_add_probe(hailodefog->srcpad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER),
                      (GstPadProbeCallback)gst_hailodefog_src_probe, hailodefog, NULL);

    // Cleanup
    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

static gboolean gst_hailodefog_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    GstHailoDefog *self = GST_HAILO_DEFOG(parent);
    gboolean ret = FALSE;
    GST_DEBUG_OBJECT(self, "Received event from sinkpad");

    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
        GST_DEBUG_OBJECT(self, "Received custom event from sinkpad");

        const GstStructure *structure = gst_event_get_structure(event);
        if (structure == NULL)
        {
            GST_ERROR_OBJECT(self, "Custom event structure is NULL");
            return FALSE;
        }

        if (gst_structure_has_name(structure, ROTATION_EVENT_NAME))
        {
            guint rotation;
            if (!gst_structure_get_uint(structure, ROTATION_EVENT_PROP_NAME, &rotation))
            {
                GST_ERROR_OBJECT(self, "Failed receiving rotation value from custom event");
                return FALSE;
            }

            GST_DEBUG_OBJECT(self, "Received custom rotation event from sinkpad: rotation %d", rotation);
            // Handle the rotation event
            // Defog is disabled when rotation is 90 or 270 degrees
            if (rotation == 1 || rotation == 3)
            {
                GST_DEBUG_OBJECT(self, "Rotating to unsupported angle (%d), defog will be disabled", rotation);
                self->m_rotated = TRUE;
                gst_hailodefog_configure_hailonet(self);
            } else {
                self->m_rotated = FALSE;
                gst_hailodefog_configure_hailonet(self);
            }
            ret = gst_pad_push_event(self->srcpad, event);
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

static GstStateChangeReturn
gst_hailodefog_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailodefog_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }
    return ret;
}

static gboolean
gst_hailodefog_erase_tensors(GstBuffer *buffer)
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
gst_hailodefog_remove_tensors(GstBuffer *buffer)
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
gst_hailodefog_payload_tensor_meta(GstBuffer *buffer, GstBuffer *payload, std::string layer_name, hailo_format_order_t format_order)
{
    GstHailoTensorMeta *meta = GST_TENSOR_META_ADD(payload);
    if (meta == nullptr)
    {
        g_error("GstHailoDefog: GstHailoTensorMeta is null when payloading\n");
    }
    memset(&meta->info, 0, sizeof(meta->info));
    memcpy(meta->info.name, layer_name.c_str(), layer_name.size());
    meta->info.format.order = format_order;
    gst_buffer_add_parent_buffer_meta(buffer, payload);
}

static std::map<std::string, GstBuffer*>
gst_hailodefog_get_tensor_meta_from_buffer(GstBuffer *buffer)
{
    std::map<std::string, GstBuffer*> meta_vector;

    gpointer state = NULL;
    GstMeta *meta;
    GstParentBufferMeta *pmeta;
    GstMapInfo info;
    if (buffer == nullptr)
    {
        g_error("gst_hailodefog_get_tensor_meta_from_buffer: Buffer is null\n");
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
gst_hailodefog_sink_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDefog *hailodefog)
{
    // Handle incoming data
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        // probe recieved something other than a buffer, pass it immediately
        return GST_PAD_PROBE_PASS;
    }

    // Get the buffer
    GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
    if (buffer == nullptr)
    {
        GST_ERROR_OBJECT(hailodefog, "Buffer is null at sink probe");
        return GST_PAD_PROBE_DROP;
    }
    if (!gst_buffer_is_writable(buffer))
    {
        GST_WARNING_OBJECT(hailodefog, "Buffer is not writable at sink probe");
    }

    // Get the network configurations
    network_config_t net_configs = hailodefog->medialib_defog->get_defog_configs().network_config;

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
    gst_hailodefog_payload_tensor_meta(buffer, y_buffer, net_configs.y_channel, HAILO_FORMAT_ORDER_NHCW);

    // UV
    GstBuffer *uv_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, uv_channel, uv_channel_size, 0, uv_channel_size, NULL, NULL);
    gst_hailodefog_payload_tensor_meta(buffer, uv_buffer, net_configs.uv_channel, HAILO_FORMAT_ORDER_NHWC);

    return GST_PAD_PROBE_PASS;
}

static GstPadProbeReturn
gst_hailodefog_src_probe(GstPad *pad, GstPadProbeInfo *info, GstHailoDefog *hailodefog)
{
    // Handle outgoing data
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        // probe recieved something other than a buffer, pass it immediately
        return GST_PAD_PROBE_PASS;
    }

    // Get the buffer
    GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
    if (buffer == nullptr)
    {
        GST_ERROR_OBJECT(hailodefog, "Buffer is null at src probe");
        return GST_PAD_PROBE_DROP;
    }
    if (!gst_buffer_is_writable(buffer))
    {
        GST_WARNING_OBJECT(hailodefog, "Buffer is not writable at src probe");
    }
    if (!hailodefog->medialib_defog->is_enabled() || hailodefog->m_rotated)
    {
        // Defog is disabled, remove any input tensors and pass
        (void)gst_hailodefog_erase_tensors(buffer);
        return GST_PAD_PROBE_PASS;
    }

    // Get the network configurations
    network_config_t net_configs = hailodefog->medialib_defog->get_defog_configs().network_config;

    // Retrieve the tensor data from the incoming buffer
    std::map<std::string, GstBuffer*> output_tensors = gst_hailodefog_get_tensor_meta_from_buffer(buffer);
    // Y
    GstBuffer *y_tensor_buffer = output_tensors[net_configs.output_y_channel];
    // UV
    GstBuffer *uv_tensor_buffer = output_tensors[net_configs.output_uv_channel];

    if (y_tensor_buffer == nullptr || uv_tensor_buffer == nullptr)
    {
        GST_INFO_OBJECT(hailodefog, "We are in closing/flushing stage, drop frame.");
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

    // Copy is done, remove the tensors from the buffer
    (void)gst_hailodefog_remove_tensors(buffer);

    return GST_PAD_PROBE_PASS;
}
