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
#include "gsthailoosd.hpp"

#include <gst/gst.h>
#include <gst/gstparamspecs.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>
#include <stddef.h>
#include <tl/expected.hpp>
#include <fstream>
#include <stdexcept>

#include "buffer_utils/buffer_utils.hpp"
#include "media_library/config_manager.hpp"
#include "osd_repository.hpp"
#include "gstmedialibcommon.hpp"
#include "media_library/cloexec_fstream.hpp"
#include "buffer_pool.hpp"
#include "gstmedialibptrs.hpp"
#include "media_library_types.hpp"
#include "privacy_mask.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailoosd_debug_category);
#define GST_CAT_DEFAULT gst_hailoosd_debug_category
#define gst_hailoosd_parent_class parent_class

static void gst_hailoosd_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoosd_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_hailoosd_dispose(GObject *object);
static void gst_hailoosd_finalize(GObject *object);

static gboolean gst_hailoosd_start(GstBaseTransform *trans);
static gboolean gst_hailoosd_stop(GstBaseTransform *trans);
static gboolean gst_hailoosd_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_hailoosd_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query);
static GstFlowReturn gst_hailoosd_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);
static void gst_hailoosd_before_transform(GstBaseTransform *trans, GstBuffer *buffer);
static gboolean init_blenders(GstHailoOsd *hailoosd);

enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STR,
    PROP_STREAM_ID,
    PROP_WAIT_FOR_WRITABLE_BUFFER,
    PROP_OSD_BLENDER,
    PROP_PM_BLENDER,
    PROP_CONFIG_MANAGER_INTERACTOR,
};

G_DEFINE_TYPE_WITH_CODE(GstHailoOsd, gst_hailoosd, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_hailoosd_debug_category, "hailoosd", 0,
                                                "debug category for hailoosd element"));

static void gst_hailoosd_class_init(GstHailoOsdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
                                       gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                            gst_caps_from_string(GST_VIDEO_CAPS_MAKE("{ NV12 }"))));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
                                       gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                            gst_caps_from_string(GST_VIDEO_CAPS_MAKE("{ NV12 }"))));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), "hailoosd - on-screen-display element",
                                          "Hailo/Tools", "Draws on-screen-display telemetry on frame.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailoosd_set_property;
    gobject_class->get_property = gst_hailoosd_get_property;
    g_object_class_install_property(
        gobject_class, PROP_STREAM_ID,
        g_param_spec_string("stream-id", "Stream id", "Stream id string", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        gobject_class, PROP_WAIT_FOR_WRITABLE_BUFFER,
        g_param_spec_boolean("wait-for-writable-buffer", "wait-for-writable-buffer",
                             "Enables the element thread to wait until incoming buffer is writable", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(
        gobject_class, PROP_CONFIG_FILE_PATH,
        g_param_spec_string("config-file-path", NULL, "Json config file path", "",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(
        gobject_class, PROP_CONFIG_STR,
        g_param_spec_string("config-string", NULL, "Json config string", "",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(
        gobject_class, PROP_CONFIG_MANAGER_INTERACTOR,
        g_param_spec_pointer("config-manager-interactor", "Config Manager Interactor pointer",
                             "Pointer to config manager interactor",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_OSD_BLENDER,
                                    g_param_spec_pointer("osd-blender", "OSD blender object",
                                                         "Pointer to OSD blender object",
                                                         (GParamFlags)(G_PARAM_READABLE)));
    g_object_class_install_property(gobject_class, PROP_PM_BLENDER,
                                    g_param_spec_pointer("privacy-mask-blender", "Privacy mask blender object",
                                                         "Pointer to privacy mask blender object",
                                                         (GParamFlags)(G_PARAM_READABLE)));

    gobject_class->dispose = gst_hailoosd_dispose;
    gobject_class->finalize = gst_hailoosd_finalize;
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_hailoosd_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_hailoosd_stop);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_hailoosd_set_caps);
    base_transform_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_hailoosd_propose_allocation);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_hailoosd_transform_ip);
    base_transform_class->before_transform = GST_DEBUG_FUNCPTR(gst_hailoosd_before_transform);
}

static void gst_hailoosd_init(GstHailoOsd *hailoosd)
{
    hailoosd->params = new GstHailoOsdParams();
    if (!init_blenders(hailoosd))
    {
        GST_ERROR_OBJECT(hailoosd, "Failed to initialize blenders from config string");
    }
}

void gst_hailoosd_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(object);

    GST_DEBUG_OBJECT(hailoosd, "set_property");

    switch (property_id)
    {
    case PROP_CONFIG_FILE_PATH:
        hailoosd->params->config_path = glib_cpp::get_string_from_gvalue(value);
        if (hailoosd->params->initialized)
        {
            cloexec::ifstream config_file(hailoosd->params->config_path);
            std::stringstream buffer;
            if (config_file.is_open())
            {
                buffer << config_file.rdbuf();
                config_file.close();
            }
            hailoosd->params->osd_configurer->configure(buffer.str());
        }
        break;
    case PROP_CONFIG_STR: {
        hailoosd->params->config_str = glib_cpp::get_string_from_gvalue(value);
        if (hailoosd->params->initialized)
        {
            hailoosd->params->osd_configurer->configure(std::string(hailoosd->params->config_str));
        }
        break;
    }
    case PROP_STREAM_ID:
        hailoosd->params->stream_id = g_value_get_string(value);
        hailoosd->params->privacy_mask_blender->set_stream_id(hailoosd->params->stream_id);
        hailoosd->params->privacy_mask_configurer->set_stream_id(hailoosd->params->stream_id);
        hailoosd->params->osd_blender->set_stream_id(hailoosd->params->stream_id);
        hailoosd->params->osd_configurer->set_stream_id(hailoosd->params->stream_id);
        break;
    case PROP_WAIT_FOR_WRITABLE_BUFFER:
        hailoosd->params->wait_for_writable_buffer = g_value_get_boolean(value);
        break;
    case PROP_CONFIG_MANAGER_INTERACTOR: {
        ConfigManagerInteractor *interactor = static_cast<ConfigManagerInteractor *>(g_value_get_pointer(value));
        hailoosd->params->privacy_mask_configurer->set_config_manager_interactor(interactor);
        hailoosd->params->osd_configurer->set_config_manager_interactor(interactor);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }

    GST_DEBUG_OBJECT(hailoosd, "finished set_property");
}

void gst_hailoosd_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(object);

    GST_DEBUG_OBJECT(hailoosd, "get_property");

    switch (property_id)
    {
    case PROP_WAIT_FOR_WRITABLE_BUFFER:
        g_value_set_boolean(value, hailoosd->params->wait_for_writable_buffer);
        break;
    case PROP_OSD_BLENDER:
        g_value_set_pointer(value, hailoosd->params->osd_configurer.get());
        break;
    case PROP_PM_BLENDER:
        g_value_set_pointer(value, hailoosd->params->privacy_mask_configurer.get());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoosd_dispose(GObject *object)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(object);
    GST_DEBUG_OBJECT(hailoosd, "dispose");
    if (hailoosd->params != nullptr)
    {
        delete hailoosd->params;
        hailoosd->params = nullptr;
    }

    G_OBJECT_CLASS(gst_hailoosd_parent_class)->dispose(object);
}

void gst_hailoosd_finalize(GObject *object)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(object);
    GST_DEBUG_OBJECT(hailoosd, "finalize");

    G_OBJECT_CLASS(gst_hailoosd_parent_class)->finalize(object);
}

static gboolean init_blenders(GstHailoOsd *hailoosd)
{
    if (hailoosd->params->osd_blender == nullptr)
    {
        if (hailoosd->params->osd_repository == nullptr)
        {
            auto repo_expected = osd::OverlayRepository::create();
            if (!repo_expected.has_value())
            {
                GST_ERROR_OBJECT(hailoosd, "Failed to create OSD repository");
                return FALSE;
            }
            hailoosd->params->osd_repository = repo_expected.value();
        }

        auto osd_blender_expected =
            osd::OsdBlender::create(hailoosd->params->stream_id, hailoosd->params->osd_repository);
        if (osd_blender_expected.has_value())
        {
            hailoosd->params->osd_blender = osd_blender_expected.value();
        }
        else
        {
            GST_ERROR_OBJECT(hailoosd, "Failed to create OSD blender");
            return FALSE;
        }
    }
    else
    {
        GST_DEBUG_OBJECT(hailoosd, "OSD Blender object already exists, skipping creation");
    }

    if (hailoosd->params->osd_configurer == nullptr)
    {
        if (hailoosd->params->osd_repository == nullptr)
        {
            auto repo_expected = osd::OverlayRepository::create();
            if (!repo_expected.has_value())
            {
                GST_ERROR_OBJECT(hailoosd, "Failed to create OSD repository");
                return FALSE;
            }
            hailoosd->params->osd_repository = repo_expected.value();
        }

        auto osd_configurer_expected =
            osd::Configurer::create(hailoosd->params->stream_id, hailoosd->params->osd_repository);
        if (osd_configurer_expected.has_value())
        {
            hailoosd->params->osd_configurer = osd_configurer_expected.value();
        }
        else
        {
            GST_ERROR_OBJECT(hailoosd, "Failed to create OSD configurer");
            return FALSE;
        }
    }
    else
    {
        GST_DEBUG_OBJECT(hailoosd, "OSD Configurer object already exists, skipping creation");
    }

    if (hailoosd->params->privacy_mask_blender == nullptr)
    {
        tl::expected<std::shared_ptr<PrivacyMask>, media_library_return> pm_blender_expected;
        pm_blender_expected = PrivacyMask::create();
        if (pm_blender_expected.has_value())
        {
            hailoosd->params->privacy_mask_blender = pm_blender_expected.value();
        }
        else
        {
            GST_ERROR_OBJECT(hailoosd, "Failed to create privacy mask blender");
            return FALSE;
        }
    }
    if (hailoosd->params->privacy_mask_configurer == nullptr)
    {
        hailoosd->params->privacy_mask_configurer = std::make_shared<PrivacyMaskConfigurer>();
    }
    else
    {
        GST_DEBUG_OBJECT(hailoosd, "Privacy Mask Blender object already exists, skipping creation");
    }

    return TRUE;
}

static gboolean gst_hailoosd_start(GstBaseTransform *trans)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(trans);

    GST_DEBUG_OBJECT(hailoosd, "start");

    if (hailoosd->params->initialized)
    {
        GST_DEBUG_OBJECT(hailoosd, "HailoOSD already initialized, skipping initialization");
        return TRUE;
    }

    hailoosd->params->initialized = true;
    return TRUE;
}

static gboolean gst_hailoosd_stop(GstBaseTransform *trans)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(trans);

    GST_DEBUG_OBJECT(hailoosd, "stop");

    return TRUE;
}

static void gst_hailoosd_wait_for_writable_buffer(GstHailoOsd *hailoosd, GstBuffer *buffer)
{
    GST_DEBUG_OBJECT(hailoosd, "Buffer (offset: %ld) is not writable, refcount: %d. waiting... ",
                     GST_BUFFER_OFFSET(buffer), GST_OBJECT_REFCOUNT(buffer));
    while (gst_buffer_is_writable(buffer) == FALSE)
    {
        // Wait for buffer to be writable
        g_usleep(100);
    }
}

static void gst_hailoosd_before_transform(GstBaseTransform *trans, GstBuffer *buffer)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(trans);

    if (gst_buffer_is_writable(buffer) == FALSE)
    {
        if (hailoosd->params->wait_for_writable_buffer)
        {
            gst_hailoosd_wait_for_writable_buffer(hailoosd, buffer);
        }
        else
        {
            GST_ERROR_OBJECT(hailoosd, "Buffer (offset: %ld) is not writable!, buffer refcount is %d. Aborting...",
                             GST_BUFFER_OFFSET(buffer), GST_OBJECT_REFCOUNT(buffer));
            throw std::runtime_error("HailoOSD -> Buffer is not writable! please validate that that the pipline is "
                                     "sharing the buffer properly, or use wait-for-writable-buffer parameter");
        }
    }

    GST_DEBUG_OBJECT(hailoosd, "Buffer is writable, refcount: %d, continuing...", GST_OBJECT_REFCOUNT(buffer));
}

static gboolean gst_hailoosd_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(trans);

    // now that caps are negotiated, get the size of the image for relative scaling
    GstVideoInfo *full_image_info = gst_video_info_new();
    gst_video_info_from_caps(full_image_info, incaps);
    auto width = GST_VIDEO_INFO_WIDTH(full_image_info);
    auto height = GST_VIDEO_INFO_HEIGHT(full_image_info);
    gst_video_info_free(full_image_info);

    media_library_return ret = hailoosd->params->osd_blender->set_frame_size(width, height);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_DEBUG_OBJECT(hailoosd, "Failed to init OSD blender with frame size %dX%d", width, height);
        return FALSE;
    }

    ret = hailoosd->params->osd_repository->set_frame_size(width, height);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_DEBUG_OBJECT(hailoosd, "Failed to init OSD repository with frame size %dX%d", width, height);
        return FALSE;
    }
    return TRUE;
}

static gboolean gst_hailoosd_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query)
{
    GstHailoOsd *hailoosd = GST_HAILO_OSD(trans);
    GST_DEBUG_OBJECT(hailoosd, "hailoosd propose allocation callback");
    gboolean ret;
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
    ret = GST_BASE_TRANSFORM_CLASS(parent_class)->propose_allocation(trans, decide_query, query);
    return ret;
}

static GstFlowReturn gst_hailoosd_transform_ip(GstBaseTransform *trans, GstBuffer *buffer)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    GstHailoOsd *hailoosd = GST_HAILO_OSD(trans);
    GstCapsPtr caps;
    GST_DEBUG_OBJECT(hailoosd, "transform_ip");

    caps = gst_pad_get_current_caps(trans->sinkpad);
    HailoMediaLibraryBufferPtr media_library_buffer = hailo_buffer_from_gst_buffer(buffer, caps);
    if (!media_library_buffer)
    {
        GST_ERROR_OBJECT(trans, "Cannot create hailo buffer from GstBuffer");
        return GST_FLOW_ERROR;
    }

    // perform privacy mask blending
    ret = hailoosd->params->privacy_mask_blender->blend(media_library_buffer);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(trans, "Failed to blend privacy mask (%d)", ret);
    }
    GST_DEBUG_OBJECT(trans, "Privacy mask blend done");

    // perform osd blending
    ret = hailoosd->params->osd_blender->blend(media_library_buffer);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(trans, "Failed to blend OSD (%d)", ret);
    }
    GST_DEBUG_OBJECT(trans, "OSD blend done");

    // check success status
    if (ret != MEDIA_LIBRARY_SUCCESS)
        return GST_FLOW_ERROR;
    return GST_FLOW_OK;
}
