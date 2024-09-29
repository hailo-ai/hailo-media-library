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

#include "gsthailofrontendbinsrc.hpp"
#include "multi_resize/gsthailomultiresize.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/isp_utils.hpp"
#include "common/gstmedialibcommon.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailofrontendbinsrc_debug_category);
#define GST_CAT_DEFAULT gst_hailofrontendbinsrc_debug_category

#define INPUT_VIDEO_IS_4K(frontendbinsrc) (frontendbinsrc->m_input_config.resolution.dimensions.destination_width == 3840 && frontendbinsrc->m_input_config.resolution.dimensions.destination_height == 2160)
#define INPUT_VIDEO_IS_FHD(frontendbinsrc) (frontendbinsrc->m_input_config.resolution.dimensions.destination_width == 1920 && frontendbinsrc->m_input_config.resolution.dimensions.destination_height == 1080)
#define HDR_IMAGING_RESOLUTION(frontendbinsrc) INPUT_VIDEO_IS_4K(frontendbinsrc) ? HDR::InputResolution::RES_4K : HDR::InputResolution::RES_FHD
#define HDR_IS_3DOL(frontendbinsrc) frontendbinsrc->m_hdr_config.dol == HDR_DOL_3
#define HDR_IMAGING_DOL(frontendbinsrc) HDR_IS_3DOL(frontendbinsrc) ? HDR::DOL::HDR_DOL_3 : HDR::DOL::HDR_DOL_2
#define HDR_HEF_PATH(frontendbinsrc) INPUT_VIDEO_IS_4K(frontendbinsrc) ? (HDR_IS_3DOL(frontendbinsrc) ? "/usr/bin/hdr_4k_3_exposures.hef" : "/usr/bin/hdr_4k_2_exposures.hef") : (HDR_IS_3DOL(frontendbinsrc) ? "/usr/bin/hdr_fhd_3_exposures.hef" : "/usr/bin/hdr_fhd_2_exposures.hef")

static void gst_hailofrontendbinsrc_set_property(GObject *object,
                                                 guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailofrontendbinsrc_get_property(GObject *object,
                                                 guint property_id, GValue *value, GParamSpec *pspec);
static GstElement *gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc);
static GstElement *gst_hailofrontendbinsrc_init_capsfilter(GstHailoFrontendBinSrc *hailofrontendbinsrc);
static GstStateChangeReturn gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition);
static GstPad *gst_hailofrontendbinsrc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps);
static void gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailofrontendbinsrc_link_elements(GstElement *element);
static void gst_hailofrontendbinsrc_dispose(GObject *object);
static void gst_hailofrontendbinsrc_reset(GstHailoFrontendBinSrc *self);
static gboolean gst_hailofrontendbinsrc_denoise_enabled_changed(GstHailoFrontendBinSrc *self, bool enabled);
static void gst_hailofrontendbinsrc_set_config(GstHailoFrontendBinSrc *self, frontend_config_t &config, std::string config_string = "");
static media_library_return gst_hailofrontendbinsrc_load_config(GstHailoFrontendBinSrc *self, std::string config_string, frontend_config_t *out_config);

enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_PRIVACY_MASK,
    PROP_CONFIG,
    PROP_HDR_CONFIG,
    PROP_HAILORT_CONFIG,
    PROP_INPUT_VIDEO_CONFIG,
    PROP_ISP_CONFIG,
};

// Pad Templates
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src_%u",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_REQUEST,
                                                                   GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoFrontendBinSrc, gst_hailofrontendbinsrc, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailofrontendbinsrc_debug_category, "hailofrontendbinsrc", 0,
                                                "debug category for hailofrontendbinsrc element"));

static void
gst_hailofrontendbinsrc_class_init(GstHailoFrontendBinSrcClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class,
                                          "frontend vision pipeline source bin", "Hailo/Media-Library", "Frontend v4l2 source bin for vision pipelines.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_dispose);

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
                                    g_param_spec_pointer("config", "Frontendbinsrc config", "Frontedbinsrc config as frontend_config_t",
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_HDR_CONFIG,
                                    g_param_spec_pointer("hdr-config", "hdr config", "HDR config as hdr_config_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));

    g_object_class_install_property(gobject_class, PROP_HAILORT_CONFIG,
                                    g_param_spec_pointer("hailort-config", "hailort config", "HailoRT config as hailort_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));
    g_object_class_install_property(gobject_class, PROP_INPUT_VIDEO_CONFIG,
                                    g_param_spec_pointer("input-video-config", "input video config", "video input config as input_video_config_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));
    g_object_class_install_property(gobject_class, PROP_ISP_CONFIG,
                                    g_param_spec_pointer("isp-config", "isp config", "isp config as isp_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_release_pad);
}

static void
gst_hailofrontendbinsrc_init(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    // Default values
    hailofrontendbinsrc->config_file_path = NULL;
    hailofrontendbinsrc->srcpads = {};
    hailofrontendbinsrc->m_elements_linked = FALSE;
    // Prepare internal elements
    // v4l2src
    hailofrontendbinsrc->m_v4l2src = gst_element_factory_make("v4l2src", NULL);
    if (nullptr == hailofrontendbinsrc->m_v4l2src)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating v4l2src element in bin!"), (NULL));
    }
    g_object_set(hailofrontendbinsrc->m_v4l2src, "device", "/dev/video0", NULL);
    g_object_set(hailofrontendbinsrc->m_v4l2src, "io-mode", 4, NULL);

    // caps
    hailofrontendbinsrc->m_capsfilter = gst_hailofrontendbinsrc_init_capsfilter(hailofrontendbinsrc);

    // queue
    hailofrontendbinsrc->m_queue = gst_hailofrontendbinsrc_init_queue(hailofrontendbinsrc);

    // frontend
    hailofrontendbinsrc->m_frontend = gst_element_factory_make("hailofrontend", NULL);
    gst_element_set_name(hailofrontendbinsrc->m_frontend, "hailofrontendelement");
    if (nullptr == hailofrontendbinsrc->m_frontend)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating hailofrontend element in bin!"), (NULL));
    }
    MediaLibraryDenoise::callbacks_t callbacks;
    callbacks.on_enable_changed = [hailofrontendbinsrc](bool enabled)
    {
        // initialize caps negotiation to be passed downstream
        auto ret = gst_hailofrontendbinsrc_denoise_enabled_changed(hailofrontendbinsrc, enabled);
        if (!ret)
            GST_ERROR_OBJECT(hailofrontendbinsrc, "Failed to respond to low-light-enhancement settings change");
    };
    hailofrontendbinsrc->observe_denoising(callbacks);

    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailofrontendbinsrc), hailofrontendbinsrc->m_v4l2src,
                     hailofrontendbinsrc->m_capsfilter,
                     hailofrontendbinsrc->m_queue,
                     hailofrontendbinsrc->m_frontend, NULL);
    hailofrontendbinsrc->m_input_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_INPUT_VIDEO);
    hailofrontendbinsrc->m_isp_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_ISP);
    hailofrontendbinsrc->m_hailort_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HAILORT);
    hailofrontendbinsrc->m_hdr_config_manager = std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_HDR);
}

static GstElement *
gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    GstElement *queue = gst_element_factory_make("queue", NULL);
    if (nullptr == queue)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
        return NULL;
    }
    // Passing 0 disables the features here
    // Note queue is leaky=2(downstream)
    g_object_set(queue, "leaky", (guint)2, NULL);
    g_object_set(queue, "max-size-time", (guint64)0, NULL);
    g_object_set(queue, "max-size-bytes", (guint)0, NULL);
    g_object_set(queue, "max-size-buffers", (guint)1, NULL);
    return queue;
}

static GstElement *
gst_hailofrontendbinsrc_init_capsfilter(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "frontendcapsfilter");
    if (nullptr == capsfilter)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating capsfilter element in bin!"), (NULL));
        return NULL;
    }
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        "width", G_TYPE_INT, 3840,
                                        "height", G_TYPE_INT, 2160,
                                        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);
    return capsfilter;
}

static void gst_hailofrontendbinsrc_set_config(GstHailoFrontendBinSrc *self, frontend_config_t &config, std::string config_string)
{
    if (self->m_elements_linked && self->m_input_config != config.input_config)
    {
        GST_ERROR_OBJECT(self, "Input Video config cannot be changed while pipeline is running");
        return;
    }
    isp_utils::set_auto_configure(config.isp_config.auto_configuration);
    isp_utils::set_isp_config_files_path(config.isp_config.isp_config_files_path);
    if (self->m_input_config != config.input_config)
    {
        // update capsfilter with input_config
        auto capsfilter = gst_bin_get_by_name(GST_BIN(self), "frontendcapsfilter");
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "NV12",
                                            "framerate", GST_TYPE_FRACTION, config.input_config.resolution.framerate, 1,
                                            "width", G_TYPE_INT, config.input_config.resolution.dimensions.destination_width,
                                            "height", G_TYPE_INT, config.input_config.resolution.dimensions.destination_height,
                                            NULL);
        g_object_set(capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);
    }
    self->m_input_config = config.input_config;
    self->m_isp_config = config.isp_config;
    self->m_hailort_config = config.hailort_config;
    self->m_hdr_config = config.hdr_config;
    if (!config_string.empty())
    {
        g_object_set(self->m_frontend, "config-string", config_string.c_str(), NULL);
    }
    denoise_config_t denoise_config;
    g_object_get(self->m_frontend, "denoise-config", &(denoise_config), NULL); // get denoise after it's parsed
    if (denoise_config.enabled && self->m_hdr_config.enabled)
    {
        GST_ERROR_OBJECT(self, "Denoise and HDR cannot be enabled at the same time");
        return;
    }
    if (denoise_config.enabled && !INPUT_VIDEO_IS_4K(self))
    {
        GST_ERROR_OBJECT(self, "Denoise is only supported for 4K resolution");
        return;
    }
}

static media_library_return gst_hailofrontendbinsrc_load_config(GstHailoFrontendBinSrc *self, std::string config_string, frontend_config_t *out_config)
{
    if (config_string.empty())
    {
        GST_ERROR_OBJECT(self, "Config string is NULL");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    media_library_return status;

    status = self->m_isp_config_manager->config_string_to_struct<isp_t>(config_string, out_config->isp_config);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to decode ISP config from json string: %s", config_string.c_str());
        return status;
    }
    status = self->m_hailort_config_manager->config_string_to_struct<hailort_t>(config_string, out_config->hailort_config);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to decode Hailort config from json string: %s", config_string.c_str());
        return status;
    }
    status = self->m_hdr_config_manager->config_string_to_struct<hdr_config_t>(config_string, out_config->hdr_config);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to decode HDR config from json string: %s", config_string.c_str());
        return status;
    }
    status = self->m_input_config_manager->config_string_to_struct<input_video_config_t>(config_string, out_config->input_config);
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to decode input video config from json string: %s", config_string.c_str());
        return status;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void gst_hailofrontendbinsrc_set_property(GObject *object, guint property_id,
                                          const GValue *value, GParamSpec *pspec)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(object);
    GST_DEBUG_OBJECT(self, "set_property");
    switch (property_id)
    {
    case PROP_CONFIG_FILE_PATH:
    {
        self->config_file_path = g_strdup(g_value_get_string(value));
        GST_DEBUG_OBJECT(self, "config_file_path: %s", self->config_file_path);
        self->config_string = gstmedialibcommon::read_json_string_from_file(self->config_file_path);

        // Load configurations
        frontend_config_t config;
        media_library_return status = gst_hailofrontendbinsrc_load_config(self, self->config_string, &config);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(self, "Failed to load config from string");
            return;
        }

        // Set configurations
        gst_hailofrontendbinsrc_set_config(self, config, self->config_string);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        self->config_string = g_value_get_string(value);
        gstmedialibcommon::strip_string_syntax(self->config_string);
        GST_DEBUG_OBJECT(self, "config-string: %s", self->config_string.c_str());

        // Load configurations
        frontend_config_t config;
        media_library_return status = gst_hailofrontendbinsrc_load_config(self, self->config_string, &config);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ERROR_OBJECT(self, "Failed to load config from string");
            return;
        }

        // Set configurations
        gst_hailofrontendbinsrc_set_config(self, config, self->config_string);
        break;
    }
    case PROP_CONFIG:
    {
        frontend_config_t *config = static_cast<frontend_config_t *>(g_value_get_pointer(value));
        g_object_set(self->m_frontend, "dewarp-config", &(config->ldc_config), NULL);
        g_object_set(self->m_frontend, "denoise-config", &(config->denoise_config), NULL);
        g_object_set(self->m_frontend, "multi-resize-config", &(config->multi_resize_config), NULL);
        gst_hailofrontendbinsrc_set_config(self, *config);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailofrontendbinsrc_get_property(GObject *object, guint property_id,
                                          GValue *value, GParamSpec *pspec)
{
    GstHailoFrontendBinSrc *hailofrontendbinsrc = GST_HAILO_FRONTEND_BINSRC(object);
    GST_DEBUG_OBJECT(hailofrontendbinsrc, "get_property");
    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH:
    {
        g_value_set_string(value, hailofrontendbinsrc->config_file_path);
        break;
    }
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailofrontendbinsrc->config_string.c_str());
        break;
    }
    case PROP_PRIVACY_MASK:
    {
        gpointer blender;
        g_object_get(hailofrontendbinsrc->m_frontend, "privacy-mask", &blender, NULL);
        g_value_set_pointer(value, blender);
        break;
    }
    case PROP_CONFIG:
    {
        gpointer frontend_config;
        g_object_get(hailofrontendbinsrc->m_frontend, "config", &frontend_config, NULL);
        g_value_set_pointer(value, frontend_config);
        break;
    }
    case PROP_HDR_CONFIG:
    {
        g_value_set_pointer(value, &hailofrontendbinsrc->m_hdr_config);
        break;
    }
    case PROP_HAILORT_CONFIG:
    {
        g_value_set_pointer(value, &hailofrontendbinsrc->m_hailort_config);
        break;
    }
    case PROP_INPUT_VIDEO_CONFIG:
    {
        g_value_set_pointer(value, &hailofrontendbinsrc->m_input_config);
        break;
    }
    case PROP_ISP_CONFIG:
    {
        g_value_set_pointer(value, &hailofrontendbinsrc->m_isp_config);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstStateChangeReturn
gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);

    switch (transition)
    {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
        if (self->m_hdr_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Stopping HDR thread");
            self->m_hdr->stop();
        }
        break;
    }
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        if (self->m_elements_linked == FALSE)
        {
            gst_hailofrontendbinsrc_link_elements(GST_ELEMENT(self));
            self->m_elements_linked = TRUE;
        }

        // setup should be done only if imx678 is available
        if (isp_utils::find_subdevice_path("imx678").empty())
        {
            GST_DEBUG_OBJECT(self, "IMX678 not found, skipping setup");
            break;
        }

        denoise_config_t denoise_config;
        g_object_get(self->m_frontend, "denoise-config", &(denoise_config), NULL);
        if (self->m_hdr_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Setting HDR configuration");
            isp_utils::setup_hdr(INPUT_VIDEO_IS_4K(self));
            isp_utils::set_hdr_configuration(INPUT_VIDEO_IS_4K(self));
        }
        else
        {
            GST_DEBUG_OBJECT(self, "Setting SDR configuration");
            isp_utils::setup_sdr();
            isp_utils::set_default_configuration();
        }

        if (denoise_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Setting denoise configuration");
            isp_utils::set_denoise_configuration();
        }

        break;
    }
    default:
        break;
    }

    result = GST_ELEMENT_CLASS(gst_hailofrontendbinsrc_parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        if (result == GST_STATE_CHANGE_FAILURE)
            break;
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_NULL_TO_READY");
        if (self->m_hdr_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Initializing HDR");
            self->m_hdr_stitcher = std::make_unique<HailortAsyncStitching>();
            bool res_stitch = self->m_hdr_stitcher->init(HDR_HEF_PATH(self), self->m_hailort_config.device_id, 1, 1000, HDR_IMAGING_DOL(self));
            if (res_stitch != 0) // 0 = success
            {
                GST_ERROR_OBJECT(self, "Failed to initialize HDR stitching");
                return GST_STATE_CHANGE_FAILURE;
            }
            self->m_hdr = std::make_unique<HDR::HDRManager>();
            bool res_hdr = self->m_hdr->init(self->m_hdr_stitcher.get(), HDR_IMAGING_DOL(self), HDR_IMAGING_RESOLUTION(self), self->m_hdr_config.ls_ratio, self->m_hdr_config.vs_ratio);
            if (!res_hdr)
            {
                GST_ERROR_OBJECT(self, "Failed to initialize HDR manager");
                return GST_STATE_CHANGE_FAILURE;
            }
        }
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
        if (self->m_hdr_config.enabled && result != GST_STATE_CHANGE_FAILURE)
        {
            GST_DEBUG_OBJECT(self, "Activate HDR thread");
            if (bool res = self->m_hdr->start(); !res)
            {
                GST_ERROR_OBJECT(self, "Failed to initialize HDR manager");
                return GST_STATE_CHANGE_FAILURE;
            }
            // sleep for 100 ms
            usleep(100000);
        }
        else
        {
            GST_DEBUG_OBJECT(self, "HDR is disabled %d, state retval %d", self->m_hdr_config.enabled, result);
        }
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_NULL");
        self->m_hdr_stitcher = nullptr;
        self->m_hdr = nullptr;
        break;
    }
    default:
        break;
    }

    return result;
}

static GstPad *
gst_hailofrontendbinsrc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name, const GstCaps *caps)
{
    GstPad *srcpad;
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc request new pad name: %s", name);

    // Get the source pad from GstHailoMultiResize that you want to expose
    GstPad *frontend_srcpad = gst_element_request_pad(self->m_frontend, templ, name, caps);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc requested frontend_srcpad: %s", GST_PAD_NAME(frontend_srcpad));

    // Create a new ghost pad and target GstHailoMultiResize source pad
    srcpad = gst_ghost_pad_new_no_target(NULL, GST_PAD_SRC);
    gboolean link_status = gst_ghost_pad_set_target(GST_GHOST_PAD(srcpad), frontend_srcpad);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc setting %s to target %s", GST_PAD_NAME(srcpad), GST_PAD_NAME(frontend_srcpad));
    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "FrontendBinSrc failed to set %s to target %s", GST_PAD_NAME(srcpad), GST_PAD_NAME(frontend_srcpad));
    }

    // Unreference the frontend_srcpad when you're done with it
    gst_object_unref(frontend_srcpad);

    // Set the new ghostpad to active and add it to the bin
    gst_pad_set_active(srcpad, TRUE);
    gst_element_add_pad(element, srcpad);
    self->srcpads.emplace_back(srcpad);

    return srcpad;
}

static void
gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    gchar *name = gst_pad_get_name(pad);
    GST_DEBUG_OBJECT(self, "Release pad: %s", name);
    g_free(name);

    GST_OBJECT_LOCK(self);

    // Find the corresponding source pad in GstHailoMultiResize
    GstPad *frontend_srcpad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));

    // Release the source pad in GstHailoFrontend
    gst_element_release_request_pad(self->m_frontend, frontend_srcpad);

    // Remove the ghost pad from GstHailoFrontendBinSrc
    gst_element_remove_pad(element, pad);

    GST_OBJECT_UNLOCK(self);

    // Unreference the frontend_srcpad when you're done with it
    gst_object_unref(frontend_srcpad);
}

static gboolean
gst_hailofrontendbinsrc_link_elements(GstElement *element)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);

    // Link the elements
    gboolean link_status = gst_element_link_many(self->m_v4l2src,
                                                 self->m_capsfilter,
                                                 self->m_queue,
                                                 self->m_frontend, NULL);

    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "Failed to link elements in bin!");
        return FALSE;
    }

    return TRUE;
}

static void
gst_hailofrontendbinsrc_dispose(GObject *object)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(object);
    GST_DEBUG_OBJECT(self, "dispose");

    if (self->config_string != "")
    {
        self->config_string.clear();
        self->config_string.shrink_to_fit();
    }
    self->m_hailort_config_manager = nullptr;
    self->m_hdr_config_manager = nullptr;

    gst_hailofrontendbinsrc_reset(self);

    G_OBJECT_CLASS(gst_hailofrontendbinsrc_parent_class)->dispose(object);
}

static void
gst_hailofrontendbinsrc_reset(GstHailoFrontendBinSrc *self)
{
    GST_DEBUG_OBJECT(self, "reset");

    // gst_hailofrontendbinsrc_release_pad will be called automatically for each srcpad
    self->srcpads.clear();
}

static gboolean
gst_hailofrontendbinsrc_denoise_enabled_changed(GstHailoFrontendBinSrc *self, bool enabled)
{
    GST_DEBUG_OBJECT(self, "Denoise enabled changed to: %d", enabled);

    // Handle enabling/disabling denoise here
    if (enabled)
    {
        // Enabled ai low-light-enhancement, disable 3dnr
        isp_utils::set_denoise_configuration();
    }
    else
    {
        // Disabled ai low-light-enhancement, enable 3dnr
        isp_utils::set_default_configuration();
    }

    return TRUE;
}
