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
#include "media_library/media_library_types.hpp"
#include "media_library/sensor_registry.hpp"
#include "multi_resize/gsthailomultiresize.hpp"
#include "media_library/isp_utils.hpp"
#include "common/gstmedialibcommon.hpp"
#include "media_library/isp_utils.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <optional>
#include <unistd.h>
#include <chrono>

GST_DEBUG_CATEGORY_STATIC(gst_hailofrontendbinsrc_debug_category);
#define GST_CAT_DEFAULT gst_hailofrontendbinsrc_debug_category

static void gst_hailofrontendbinsrc_set_property(GObject *object, guint property_id, const GValue *value,
                                                 GParamSpec *pspec);
static void gst_hailofrontendbinsrc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstElement *gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc);
static GstStateChangeReturn gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition);
static GstPad *gst_hailofrontendbinsrc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                       const GstCaps *caps);
static void gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad);
static gboolean gst_hailofrontendbinsrc_link_elements(GstElement *element);
static void gst_hailofrontendbinsrc_dispose(GObject *object);
static gboolean gst_hailofrontendbinsrc_denoise_enabled_changed(GstHailoFrontendBinSrc *self, bool enabled);
static void gst_hailofrontendbinsrc_set_config(GstHailoFrontendBinSrc *self, frontend_config_t &config,
                                               std::string config_string = "");
static std::optional<frontend_config_t> gst_hailofrontendbinsrc_load_config(GstHailoFrontendBinSrc *self,
                                                                            const std::string &config_string);

enum
{
    PROP_0,
    PROP_CONFIG_FILE_PATH,
    PROP_CONFIG_STRING,
    PROP_CONFIG,
    PROP_HDR_CONFIG,
    PROP_HAILORT_CONFIG,
    PROP_INPUT_VIDEO_CONFIG,
    PROP_ISP_CONFIG,
    PROP_FREEZE,
    PROP_NUM_BUFFERS
};

// Pad Templates
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u", GST_PAD_SRC, GST_PAD_REQUEST, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstHailoFrontendBinSrc, gst_hailofrontendbinsrc, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_hailofrontendbinsrc_debug_category, "hailofrontendbinsrc", 0,
                                                "debug category for hailofrontendbinsrc element"));

static int round_up_to_multiple_inner(int num_to_round, int multiple)
{
    if (multiple == 0)
        return num_to_round;

    int remainder = num_to_round % multiple;
    if (remainder == 0)
        return num_to_round;

    return num_to_round + multiple - remainder;
}

static void gst_hailofrontendbinsrc_class_init(GstHailoFrontendBinSrcClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class, "frontend vision pipeline source bin", "Hailo/Media-Library",
                                          "Frontend v4l2 source bin for vision pipelines.",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_dispose);

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
        g_param_spec_pointer("config", "Frontendbinsrc config", "Frontedbinsrc config as frontend_config_t",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_HDR_CONFIG,
                                    g_param_spec_pointer("hdr-config", "hdr config", "HDR config as hdr_config_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));

    g_object_class_install_property(gobject_class, PROP_HAILORT_CONFIG,
                                    g_param_spec_pointer("hailort-config", "hailort config",
                                                         "HailoRT config as hailort_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));
    g_object_class_install_property(gobject_class, PROP_INPUT_VIDEO_CONFIG,
                                    g_param_spec_pointer("input-video-config", "input video config",
                                                         "video input config as input_video_config_t",
                                                         (GParamFlags)(G_PARAM_READABLE)));
    g_object_class_install_property(
        gobject_class, PROP_ISP_CONFIG,
        g_param_spec_pointer("isp-config", "isp config", "isp config as isp_t", (GParamFlags)(G_PARAM_READABLE)));
    g_object_class_install_property(
        gobject_class, PROP_FREEZE,
        g_param_spec_boolean("freeze", "Freeze", "Freeze the image", FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(gobject_class, PROP_NUM_BUFFERS,
                                    g_param_spec_int("num-buffers", "number of buffers",
                                                     "Number of buffers to output before sending EOS (-1 = unlimited)",
                                                     -1, G_MAXINT, -1, (GParamFlags)(G_PARAM_READWRITE)));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_release_pad);
}

static void gst_hailofrontendbinsrc_init(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    // Default values
    hailofrontendbinsrc->params = new GstHailoFrontendBinSrcParams();
    hailofrontendbinsrc->params->m_v4l2_ctrl_manager = std::make_shared<v4l2::v4l2ControlManager>();
    hailofrontendbinsrc->params->m_pre_isp_denoise =
        std::make_shared<MediaLibraryPreIspDenoise>(hailofrontendbinsrc->params->m_v4l2_ctrl_manager);
    // Prepare internal elements
    // v4l2src
    hailofrontendbinsrc->params->m_v4l2src = gst_element_factory_make("v4l2src", NULL);
    if (nullptr == hailofrontendbinsrc->params->m_v4l2src)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating v4l2src element in bin!"), (NULL));
    }
    g_object_set(hailofrontendbinsrc->params->m_v4l2src, "io-mode", 4, NULL);

    // caps
    hailofrontendbinsrc->params->m_capsfilter = gst_element_factory_make("capsfilter", "frontendcapsfilter");
    // queue
    hailofrontendbinsrc->params->m_queue = gst_hailofrontendbinsrc_init_queue(hailofrontendbinsrc);

    // frontend
    hailofrontendbinsrc->params->m_frontend = gst_element_factory_make("hailofrontend", NULL);
    gst_element_set_name(hailofrontendbinsrc->params->m_frontend, "hailofrontendelement");
    if (nullptr == hailofrontendbinsrc->params->m_frontend)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating hailofrontend element in bin!"),
                          (NULL));
    }
    MediaLibraryDenoise::callbacks_t callbacks;
    callbacks.on_enable_changed = [hailofrontendbinsrc](bool enabled) {
        // initialize caps negotiation to be passed downstream
        auto ret = gst_hailofrontendbinsrc_denoise_enabled_changed(hailofrontendbinsrc, enabled);
        if (!ret)
            GST_ERROR_OBJECT(hailofrontendbinsrc, "Failed to respond to low-light-enhancement settings change");
    };

    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailofrontendbinsrc), hailofrontendbinsrc->params->m_v4l2src,
                     hailofrontendbinsrc->params->m_capsfilter, hailofrontendbinsrc->params->m_queue,
                     hailofrontendbinsrc->params->m_frontend, NULL);
    hailofrontendbinsrc->params->m_frontend_config_manager =
        std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_FRONTEND);
}

static GstElement *gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc)
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

static void gst_hailofrontendbinsrc_set_config(GstHailoFrontendBinSrc *self, frontend_config_t &config,
                                               std::string config_string)
{
    if (self->params->m_elements_linked && self->params->m_frontend_config.input_config != config.input_config)
    {
        GST_ERROR_OBJECT(self, "Input Video config cannot be changed while pipeline is running");
        return;
    }

    if (config.denoise_config.enabled && config.hdr_config.enabled)
    {
        GST_ERROR_OBJECT(self, "Denoise and HDR cannot be enabled at the same time");
        return;
    }

    self->params->m_v4l2_ctrl_manager->set_sensor_index(config.input_config.sensor_index);
    auto device_path = SensorRegistry::get_instance().get_video_device_path(config.input_config.sensor_index);
    if (!device_path.has_value())
    {
        GST_ERROR_OBJECT(self, "Failed to get video device path");
        return;
    }
    g_object_set(self->params->m_v4l2src, "device", device_path.value().c_str(), NULL);

    isp_utils::set_isp_config_files_path(config.isp_config.isp_config_files_path);
    if (self->params->m_frontend_config.input_config != config.input_config)
    {
        static constexpr int RESOLUTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK = 16;
        auto adjusted_width = round_up_to_multiple_inner(config.input_config.resolution.dimensions.destination_width,
                                                         RESOLUTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK);
        auto adjusted_height = round_up_to_multiple_inner(config.input_config.resolution.dimensions.destination_height,
                                                          RESOLUTION_MULTIPLE_REQUIRED_BY_DENOISE_NETWORK);
        // update capsfilter with input_config
        GstCapsPtr caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "framerate",
                                              GST_TYPE_FRACTION, config.input_config.resolution.framerate, 1, "width",
                                              G_TYPE_INT, adjusted_width, "height", G_TYPE_INT, adjusted_height, NULL);
        g_object_set(self->params->m_capsfilter, "caps", caps.get(), NULL);
    }

    self->params->m_frontend_config = config;
    if (!config_string.empty())
    {
        g_object_set(self->params->m_frontend, "config-string", config_string.c_str(), NULL);
        if (self->params->m_pre_isp_denoise->configure(config_string) != MEDIA_LIBRARY_SUCCESS)
            GST_ERROR_OBJECT(self, "configuration error: Pre ISP Denoise");
    }
    g_object_set(self->params->m_frontend, "denoise-config", &(self->params->m_frontend_config), NULL);
}

static std::optional<frontend_config_t> gst_hailofrontendbinsrc_load_config(GstHailoFrontendBinSrc *self,
                                                                            const std::string &config_string)
{
    if (config_string.empty())
    {
        GST_ERROR_OBJECT(self, "Config string is NULL");
        return std::nullopt;
    }

    frontend_config_t out_config;
    if (self->params->m_frontend_config_manager->config_string_to_struct<frontend_config_t>(
            config_string, out_config) != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to decode ISP config from json string: %s", config_string.c_str());
        return std::nullopt;
    }

    return std::make_optional(out_config);
}

void gst_hailofrontendbinsrc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(object);
    std::lock_guard<std::mutex> lock(self->params->m_config_mutex);
    GST_DEBUG_OBJECT(self, "set_property");
    switch (property_id)
    {
    case PROP_CONFIG_FILE_PATH: {
        self->params->config_file_path = glib_cpp::get_string_from_gvalue(value);
        GST_DEBUG_OBJECT(self, "config_file_path: %s", self->params->config_file_path.c_str());
        self->params->config_string = gstmedialibcommon::read_json_string_from_file(self->params->config_file_path);

        // Load configurations
        auto config = gst_hailofrontendbinsrc_load_config(self, self->params->config_string);
        if (!config.has_value())
        {
            GST_ERROR_OBJECT(self, "Failed to load config from string");
            return;
        }

        // Set configurations
        gst_hailofrontendbinsrc_set_config(self, config.value(), self->params->config_string);
        break;
    }
    case PROP_CONFIG_STRING: {
        self->params->config_string = g_value_get_string(value);
        gstmedialibcommon::strip_string_syntax(self->params->config_string);
        GST_DEBUG_OBJECT(self, "config-string: %s", self->params->config_string.c_str());

        // Load configurations
        auto config = gst_hailofrontendbinsrc_load_config(self, self->params->config_string);
        if (!config.has_value())
        {
            GST_ERROR_OBJECT(self, "Failed to load config from string");
            return;
        }

        // Set configurations
        gst_hailofrontendbinsrc_set_config(self, config.value(), self->params->config_string);
        break;
    }
    case PROP_CONFIG: {
        frontend_config_t *config = static_cast<frontend_config_t *>(g_value_get_pointer(value));
        g_object_set(self->params->m_frontend, "dewarp-config", &(config->ldc_config), NULL);
        g_object_set(self->params->m_frontend, "denoise-config", config, NULL);
        g_object_set(self->params->m_frontend, "multi-resize-config", &(config->multi_resize_config), NULL);
        self->params->m_pre_isp_denoise->configure(config->denoise_config, config->hailort_config,
                                                   config->input_config);
        gst_hailofrontendbinsrc_set_config(self, *config);
        break;
    }
    case PROP_FREEZE: {
        g_object_set(self->params->m_frontend, "freeze", g_value_get_boolean(value), NULL);
        break;
    }
    case PROP_NUM_BUFFERS: {
        g_object_set(self->params->m_v4l2src, "num-buffers", g_value_get_int(value), NULL);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailofrontendbinsrc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoFrontendBinSrc *hailofrontendbinsrc = GST_HAILO_FRONTEND_BINSRC(object);
    std::lock_guard<std::mutex> lock(hailofrontendbinsrc->params->m_config_mutex);
    GST_DEBUG_OBJECT(hailofrontendbinsrc, "get_property");
    switch (property_id)
    {
    // Handle property retrievals here
    case PROP_CONFIG_FILE_PATH: {
        g_value_set_string(value, hailofrontendbinsrc->params->config_file_path.c_str());
        break;
    }
    case PROP_CONFIG_STRING: {
        g_value_set_string(value, hailofrontendbinsrc->params->config_string.c_str());
        break;
    }
    case PROP_CONFIG: {
        gpointer frontend_config;
        g_object_get(hailofrontendbinsrc->params->m_frontend, "config", &frontend_config, NULL);
        g_value_set_pointer(value, frontend_config);
        break;
    }
    case PROP_HDR_CONFIG: {
        g_value_set_pointer(value, &hailofrontendbinsrc->params->m_frontend_config.hdr_config);
        break;
    }
    case PROP_HAILORT_CONFIG: {
        g_value_set_pointer(value, &hailofrontendbinsrc->params->m_frontend_config.hailort_config);
        break;
    }
    case PROP_INPUT_VIDEO_CONFIG: {
        g_value_set_pointer(value, &hailofrontendbinsrc->params->m_frontend_config.input_config);
        break;
    }
    case PROP_ISP_CONFIG: {
        g_value_set_pointer(value, &hailofrontendbinsrc->params->m_frontend_config.isp_config);
        break;
    }
    case PROP_FREEZE: {
        g_object_get(hailofrontendbinsrc->params->m_frontend, "freeze", &value, NULL);
        break;
    }
    case PROP_NUM_BUFFERS: {
        g_object_get(hailofrontendbinsrc->params->m_v4l2src, "num-buffers", &value, NULL);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstStateChangeReturn gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    std::lock_guard<std::mutex> lock(self->params->m_config_mutex);

    gpointer value_ptr;
    g_object_get(self->params->m_frontend, "denoise-config", &value_ptr, NULL);
    denoise_config_t *denoise_config = reinterpret_cast<denoise_config_t *>(value_ptr);
    if (denoise_config == nullptr)
    {
        GST_ERROR_OBJECT(self, "Failed to get denoise config");
        return GST_STATE_CHANGE_FAILURE;
    }

    switch (transition)
    {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
        if (self->params->m_frontend_config.hdr_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Stopping HDR thread");
            self->params->m_hdr->stop();
        }
        else if (self->params->m_pre_isp_denoise->is_enabled())
        {
            GST_DEBUG_OBJECT(self, "Stopping Pre-ISP Denoise");
            self->params->m_pre_isp_denoise->stop();
        }
        break;
    }
    case GST_STATE_CHANGE_NULL_TO_READY: {
        if (self->params->m_elements_linked == FALSE)
        {
            gst_hailofrontendbinsrc_link_elements(GST_ELEMENT(self));
            self->params->m_elements_linked = TRUE;
        }

        // setup should be done only if imx* is available
        if (!isp_utils::get_sensor_type().has_value())
        {
            GST_DEBUG_OBJECT(self, "IMX not found, skipping setup");
            break;
        }
        if (denoise_config->enabled && self->params->m_frontend_config.hdr_config.enabled)
        {
            GST_ERROR_OBJECT(self, "Denoise and HDR cannot be enabled at the same time");
            return GST_STATE_CHANGE_FAILURE;
        }
        else if (self->params->m_frontend_config.hdr_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Setting HDR configuration");
        }
        else if (self->params->m_pre_isp_denoise->is_enabled())
        {
            GST_DEBUG_OBJECT(self, "Intializing Pre-ISP Denoise");
            if (self->params->m_pre_isp_denoise->init() != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to initialize Pre-ISP Denoise");
                return GST_STATE_CHANGE_FAILURE;
            }
        }
        else
        {
            GST_DEBUG_OBJECT(self, "Setting SDR configuration");
            if (MEDIA_LIBRARY_SUCCESS != isp_utils::setup_sdr(self->params->m_frontend_config.input_config.resolution,
                                                              self->params->m_v4l2_ctrl_manager))
            {
                GST_ERROR_OBJECT(self, "Failed to setup SDR");
                return GST_STATE_CHANGE_FAILURE;
            }
        }
        break;
    }
    default:
        break;
    }

    result = GST_ELEMENT_CLASS(gst_hailofrontendbinsrc_parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY: {
        if (result == GST_STATE_CHANGE_FAILURE)
            break;
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_NULL_TO_READY");
        if (self->params->m_frontend_config.hdr_config.enabled)
        {
            GST_DEBUG_OBJECT(self, "Initializing HDR");
            self->params->m_hdr = std::make_unique<HdrManager>(self->params->m_v4l2_ctrl_manager);
            bool res_hdr = self->params->m_hdr->init(self->params->m_frontend_config);
            if (!res_hdr)
            {
                GST_ERROR_OBJECT(self, "Failed to initialize HDR manager");
                return GST_STATE_CHANGE_FAILURE;
            }
        }
        else if (self->params->m_pre_isp_denoise->is_enabled())
        {
            GST_DEBUG_OBJECT(self, "Pre ISP Denoise intialized");
        }
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
        if (self->params->m_frontend_config.hdr_config.enabled && result != GST_STATE_CHANGE_FAILURE)
        {
            GST_DEBUG_OBJECT(self, "Activate HDR thread");
            if (bool res = self->params->m_hdr->start(); !res)
            {
                GST_ERROR_OBJECT(self, "Failed to initialize HDR manager");
                return GST_STATE_CHANGE_FAILURE;
            }

            GST_DEBUG_OBJECT(self, "Activate HDR forward timestamp");
        }
        else if (self->params->m_pre_isp_denoise->is_enabled() && result != GST_STATE_CHANGE_FAILURE)
        {
            GST_DEBUG_OBJECT(self, "Activate Pre-ISP Denoise");
            if (self->params->m_pre_isp_denoise->start() != MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(self, "Failed to start Pre-ISP Denoise");
                return GST_STATE_CHANGE_FAILURE;
            }
        }
        else
        {
            GST_DEBUG_OBJECT(self, "HDR and Pre-ISP Denoise are disabled %d, state retval %d",
                             self->params->m_frontend_config.hdr_config.enabled, result);
        }
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_NULL");
        self->params->m_hdr = nullptr;
        self->params->m_pre_isp_denoise->deinit();
        break;
    }
    default:
        break;
    }

    return result;
}

static GstPad *gst_hailofrontendbinsrc_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                       const GstCaps *caps)
{
    GstPadPtr srcpad;
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc request new pad name: %s", name);

    // Get the source pad from GstHailoMultiResize that you want to expose
    GstPadPtr frontend_srcpad = gst_element_request_pad(self->params->m_frontend, templ, name, caps);
    const auto frontend_srcpad_name = glib_cpp::ptrs::get_pad_name(frontend_srcpad);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc requested frontend_srcpad: %s", frontend_srcpad_name);

    // Create a new ghost pad and target GstHailoMultiResize source pad
    srcpad = gst_ghost_pad_new_no_target(NULL, GST_PAD_SRC);
    const auto srcpad_name = glib_cpp::ptrs::get_pad_name(srcpad);
    gboolean link_status = gst_ghost_pad_set_target(GST_GHOST_PAD(srcpad.get()), frontend_srcpad);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc setting %s to target %s", srcpad_name, frontend_srcpad_name);
    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "FrontendBinSrc failed to set %s to target %s", srcpad_name, frontend_srcpad_name);
    }

    // Set the new ghostpad to active and add it to the bin
    gst_pad_set_active(srcpad, TRUE);
    glib_cpp::ptrs::add_pad_to_element(element, srcpad);
    self->params->srcpads.emplace_back(srcpad);

    return srcpad;
}

static void gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    GST_DEBUG_OBJECT(self, "Release pad: %s", glib_cpp::get_name(pad).c_str());

    GST_OBJECT_LOCK(self);

    // Find the corresponding source pad in GstHailoMultiResize
    GstPadPtr frontend_srcpad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));

    // Release the source pad in GstHailoFrontend
    gst_element_release_request_pad(self->params->m_frontend, frontend_srcpad);

    // Remove the ghost pad from GstHailoFrontendBinSrc
    gst_element_remove_pad(element, pad);

    GST_OBJECT_UNLOCK(self);
}

static gboolean gst_hailofrontendbinsrc_link_elements(GstElement *element)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);

    // Link the elements
    gboolean link_status = gst_element_link_many(self->params->m_v4l2src, self->params->m_capsfilter,
                                                 self->params->m_queue, self->params->m_frontend, NULL);

    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "Failed to link elements in bin!");
        return FALSE;
    }

    return TRUE;
}

static void gst_hailofrontendbinsrc_dispose(GObject *object)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(object);
    GST_DEBUG_OBJECT(self, "dispose");
    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }

    G_OBJECT_CLASS(gst_hailofrontendbinsrc_parent_class)->dispose(object);
}

static gboolean gst_hailofrontendbinsrc_denoise_enabled_changed(GstHailoFrontendBinSrc *self, bool enabled)
{
    std::lock_guard<std::mutex> lock(self->params->m_config_mutex);
    GST_DEBUG_OBJECT(self, "Denoise enabled changed to: %d", enabled);
    return TRUE;
}
