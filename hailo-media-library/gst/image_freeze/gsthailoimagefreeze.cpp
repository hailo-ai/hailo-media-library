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

#include "gsthailoimagefreeze.hpp"

#include <gst/gst.h>
#include <gst/gstparamspecs.h>
#include <gst/video/video-info.h>
#include <string.h>
#include <memory>
#include <utility>

#include <hailo/hailodsp_base.h>
#include "common/gstmedialibcommon.hpp"
#include "buffer_utils/buffer_utils.hpp"
#include "media_library/dsp_utils.hpp"
#include "buffer_pool.hpp"
#include "media_library_buffer.hpp"
#include "media_library_types.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailo_image_freeze_debug);
#define GST_CAT_DEFAULT gst_hailo_image_freeze_debug

// Pad Templates
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

#define _do_init                                                                                                       \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_image_freeze_debug, "hailoimagefreeze", 0, "Hailo Image Freeze element");

#define gst_hailo_image_freeze_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoImageFreeze, gst_hailo_image_freeze, GST_TYPE_ELEMENT, _do_init);

static void gst_hailo_image_freeze_dispose(GObject *object);
static void gst_hailo_image_freeze_set_property(GObject *object, guint property_id, const GValue *value,
                                                GParamSpec *pspec);
static void gst_hailo_image_freeze_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static gboolean gst_hailo_image_freeze_sink_event(GstPad *pad, GstObject *parent, GstEvent *gst_event);
static GstFlowReturn gst_hailo_image_freeze_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer);
static GstFlowReturn create_and_initialize_buffer_pools(GstHailoImageFreeze *self, GstCapsPtr &caps);

enum
{
    PROP_PAD_0,
    PROP_FREEZE
};

static void gst_hailo_image_freeze_class_init(GstHailoImageFreezeClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    // Pad templates
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    // Set metadata
    gst_element_class_set_static_metadata(element_class, "image freeze", "Hailo/Media-Library", "Image Freeze element",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailo_image_freeze_set_property;
    gobject_class->get_property = gst_hailo_image_freeze_get_property;
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailo_image_freeze_dispose);
    g_object_class_install_property(
        gobject_class, PROP_FREEZE,
        g_param_spec_boolean("freeze", "Freeze", "Freeze the image", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
}

static void gst_hailo_image_freeze_init(GstHailoImageFreeze *image_freeze)
{
    GST_DEBUG_OBJECT(image_freeze, "init");
    image_freeze->params = new GstHailoImageFreezeParams();

    image_freeze->params->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    image_freeze->params->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    gst_pad_set_chain_function(image_freeze->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_image_freeze_chain));
    gst_pad_set_event_function(image_freeze->params->sinkpad, GST_DEBUG_FUNCPTR(gst_hailo_image_freeze_sink_event));

    GST_PAD_SET_PROXY_CAPS(image_freeze->params->sinkpad.get());
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(image_freeze), image_freeze->params->sinkpad);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(image_freeze), image_freeze->params->srcpad);
}

static void gst_hailo_image_freeze_dispose(GObject *object)
{
    GstHailoImageFreeze *self = GST_HAILO_IMAGE_FREEZE(object);
    GST_DEBUG_OBJECT(self, "dispose");
    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void gst_hailo_image_freeze_set_property(GObject *object, guint property_id, const GValue *value,
                                                GParamSpec *pspec)
{
    GstHailoImageFreeze *self = GST_HAILO_IMAGE_FREEZE(object);

    switch (property_id)
    {
    case PROP_FREEZE: {
        bool new_freeze = g_value_get_boolean(value);
        GST_INFO_OBJECT(self, "Setting freeze property to %d", new_freeze);
        GST_OBJECT_LOCK(self);
        if (self->params->m_freeze != new_freeze)
        {
            // Drop the cached snapshot on every state transition so the next
            // freeze cycle captures a fresh frame rather than replaying a
            // stale one. Re-asserting the same state is a no-op.
            self->params->frozen_buffer = nullptr;
            self->params->m_freeze = new_freeze;
        }
        GST_OBJECT_UNLOCK(self);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_hailo_image_freeze_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoImageFreeze *self = GST_HAILO_IMAGE_FREEZE(object);

    switch (property_id)
    {
    case PROP_FREEZE: {
        GST_OBJECT_LOCK(self);
        g_value_set_boolean(value, self->params->m_freeze);
        GST_OBJECT_UNLOCK(self);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_hailo_image_freeze_sink_event(GstPad *pad, GstObject *parent, GstEvent *gst_event)
{
    GstEventPtr event = gst_event;
    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS)
    {
        GstHailoImageFreeze *self = GST_HAILO_IMAGE_FREEZE(parent);
        GST_DEBUG_OBJECT(self, "Received caps event from sinkpad");
        GstCapsPtr caps = glib_cpp::ptrs::parse_event_caps(event);
        if (create_and_initialize_buffer_pools(self, caps) != GST_FLOW_OK)
        {
            GST_ERROR_OBJECT(self, "Failed to create buffer pool after caps event");
            return FALSE;
        }
    }

    // send to be handled by default handler anyway
    return glib_cpp::ptrs::pad_event_default(pad, parent, event);
}

static GstFlowReturn create_and_initialize_buffer_pools(GstHailoImageFreeze *self, GstCapsPtr &caps)
{
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps))
    {
        GST_WARNING_OBJECT(self, "Failed to get video info from caps");
        return GST_FLOW_ERROR;
    }
    size_t width = GST_VIDEO_INFO_WIDTH(&info);
    size_t height = GST_VIDEO_INFO_HEIGHT(&info);

    GST_OBJECT_LOCK(self);
    if (self->params->m_buffer_pool != nullptr && self->params->m_buffer_pool->get_width() == width &&
        self->params->m_buffer_pool->get_height() == height)
    {
        GST_OBJECT_UNLOCK(self);
        return GST_FLOW_OK;
    }

    GST_INFO_OBJECT(self, "Creating buffer pool with width %zu and height %zu", width, height);
    // Any cached snapshot was allocated from the old pool at the old
    // dimensions; pushing it downstream under the new caps would mismatch
    // size/stride. Drop it so the next frozen frame is recaptured.
    self->params->frozen_buffer = nullptr;
    // Match the stride convention used by dewarp / multi_resize so the
    // snapshot pool is compatible with upstream DSP-aligned buffers.
    auto bytes_per_line = dsp_utils::get_dsp_desired_stride_from_width(width);
    self->params->m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
        width, height, HAILO_FORMAT_NV12, 1, HAILO_MEMORY_TYPE_DMABUF, bytes_per_line, "image_freeze_output");
    if (self->params->m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "ImageFreeze element Failed to init buffer pool");
        GST_OBJECT_UNLOCK(self);
        return GST_FLOW_ERROR;
    }
    GST_OBJECT_UNLOCK(self);

    return GST_FLOW_OK;
}

static GstFlowReturn gst_hailo_image_freeze_chain(GstPad *pad, GstObject *parent, GstBuffer *gst_buffer)
{
    GstBufferPtr buffer = gst_buffer;
    GstHailoImageFreeze *self = GST_HAILO_IMAGE_FREEZE(parent);

    // Serialize property toggles against the in-flight snapshot. Released
    // before any GstBuffer wrapping/push so we never hold the object lock
    // across downstream peer operations.
    GST_OBJECT_LOCK(self);
    GST_DEBUG_OBJECT(self, "Chain - Received buffer from sinkpad. freeze: %d", self->params->m_freeze);

    HailoMediaLibraryBufferPtr snapshot_to_emit;
    if (self->params->m_freeze)
    {
        GstCapsPtr current_caps = gst_pad_get_current_caps(pad);
        HailoMediaLibraryBufferPtr input_buffer = hailo_buffer_from_gst_buffer(buffer, current_caps.get());
        if (!self->params->frozen_buffer)
        {
            GST_INFO_OBJECT(self, "Freezing buffer, creating new buffer and copying data");
            self->params->frozen_buffer = std::make_shared<hailo_media_library_buffer>();
            if (self->params->m_buffer_pool->acquire_buffer(self->params->frozen_buffer) != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to acquire buffer to freeze");
                GST_OBJECT_UNLOCK(self);
                return GST_FLOW_ERROR;
            }

            dsp_status copy_status = dsp_utils::perform_dsp_copy(input_buffer->buffer_data.get(),
                                                                 self->params->frozen_buffer->buffer_data.get());
            if (copy_status != DSP_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "DSP copy into frozen buffer failed with status %d", copy_status);
                self->params->frozen_buffer = nullptr;
                GST_OBJECT_UNLOCK(self);
                return GST_FLOW_ERROR;
            }
            self->params->frozen_buffer->copy_metadata_from(input_buffer);
        }
        else
        {
            snapshot_to_emit = self->params->frozen_buffer;
        }
    }
    else
    {
        GST_DEBUG_OBJECT(self, "Not freezing, passing buffer as is");
    }
    GST_OBJECT_UNLOCK(self);

    if (snapshot_to_emit)
    {
        GstCapsPtr current_caps = gst_pad_get_current_caps(pad);
        GstBufferPtr frozen_buffer = gst_buffer_from_hailo_buffer(snapshot_to_emit, current_caps.get());
        GstClockTime pts = GST_BUFFER_PTS(buffer);
        GstClockTime dts = GST_BUFFER_DTS(buffer);
        GstClockTime duration = GST_BUFFER_DURATION(buffer);
        buffer = std::move(frozen_buffer);
        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DTS(buffer) = dts;
        GST_BUFFER_DURATION(buffer) = duration;
    }

    return glib_cpp::ptrs::push_buffer_to_pad(self->params->srcpad, buffer);
}
