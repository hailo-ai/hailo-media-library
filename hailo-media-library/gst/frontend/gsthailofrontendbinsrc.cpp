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
#include "isp_manager.hpp"
#include "media_library/media_library_types.hpp"
#include "media_library/sensor_registry.hpp"
#include "media_library_logger.hpp"
#include "media_library/isp_utils.hpp"
#include "media_library/config_manager.hpp"
#include "common/gstmedialibcommon.hpp"
#include "media_library/isp_utils.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <dlfcn.h>
#include <optional>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC(gst_hailofrontendbinsrc_debug_category);
#define GST_CAT_DEFAULT gst_hailofrontendbinsrc_debug_category

static constexpr LoggerType MODULE_NAME = LoggerType::GstFrontendBin;

// Static variables to manage v4l2src class patching across multiple instances
static GstStateChangeReturn (*g_original_v4l2src_change_state)(GstElement *, GstStateChange) = nullptr;
static int g_v4l2src_patch_refcount = 0;
static std::mutex g_v4l2src_patch_mutex;

static void gst_hailofrontendbinsrc_set_property(GObject *object, guint property_id, const GValue *value,
                                                 GParamSpec *pspec);
static void gst_hailofrontendbinsrc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstElement *gst_hailofrontendbinsrc_init_queue(GstHailoFrontendBinSrc *hailofrontendbinsrc);
static GstStateChangeReturn gst_hailofrontendbinsrc_change_state(GstElement *element, GstStateChange transition);
static GstStateChangeReturn gst_hailofrontendbinsrc_v4l2src_change_state_wrapper(GstElement *element,
                                                                                 GstStateChange transition);
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
static bool set_dummy_profile_in_gst_mode(GstHailoFrontendBinSrc *self, const frontend_config_t &frontend_config);

ConfigManagerInteractor *gst_mode_config_manager_interactor =
    nullptr; // global is the best way to access from encoders as well in gst mode

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
    PROP_CONFIG_MANAGER_INTERACTOR,
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
    g_object_class_install_property(
        gobject_class, PROP_CONFIG_MANAGER_INTERACTOR,
        g_param_spec_pointer("config-manager-interactor", "Config Manager Interactor pointer",
                             "Pointer to config manager interactor",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailofrontendbinsrc_release_pad);
}

static void gst_hailofrontendbinsrc_init(GstHailoFrontendBinSrc *hailofrontendbinsrc)
{
    // Default values
    hailofrontendbinsrc->params = new GstHailoFrontendBinSrcParams();
    hailofrontendbinsrc->params->m_v4l2_ctrl_manager = std::make_shared<v4l2::v4l2ControlManager>();
    hailofrontendbinsrc->params->m_isp_manager = std::make_unique<IspManager>();
    hailofrontendbinsrc->params->m_hdr =
        std::make_unique<HdrManager>(*hailofrontendbinsrc->params->m_isp_manager.get());
    hailofrontendbinsrc->params->m_pre_isp_denoise =
        std::make_shared<MediaLibraryPreIspDenoise>(*hailofrontendbinsrc->params->m_isp_manager.get());
    // Prepare internal elements
    // v4l2src
    hailofrontendbinsrc->params->m_v4l2src = gst_element_factory_make("v4l2src", "v4l2src");
    if (nullptr == hailofrontendbinsrc->params->m_v4l2src)
    {
        GST_ELEMENT_ERROR(hailofrontendbinsrc, RESOURCE, FAILED, ("Failed creating v4l2src element in bin!"), (NULL));
    }
    g_object_set(hailofrontendbinsrc->params->m_v4l2src, "io-mode", 4, NULL);

    // Monkey patch v4l2src change_state to call modify_isp_config_files before NULL->READY transition
    {
        std::lock_guard<std::mutex> lock(g_v4l2src_patch_mutex);

        if (g_original_v4l2src_change_state == nullptr)
        {
            GstElementClass *v4l2src_class = GST_ELEMENT_GET_CLASS(hailofrontendbinsrc->params->m_v4l2src);
            g_original_v4l2src_change_state = v4l2src_class->change_state;
            v4l2src_class->change_state = gst_hailofrontendbinsrc_v4l2src_change_state_wrapper;
        }

        ++g_v4l2src_patch_refcount;
    }

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

    gst_mode_config_manager_interactor = nullptr;
    // Add elements and pads in the bin
    gst_bin_add_many(GST_BIN(hailofrontendbinsrc), hailofrontendbinsrc->params->m_v4l2src,
                     hailofrontendbinsrc->params->m_capsfilter, hailofrontendbinsrc->params->m_queue,
                     hailofrontendbinsrc->params->m_frontend, NULL);
    hailofrontendbinsrc->params->m_frontend_config_parser =
        std::make_shared<ConfigParser>(ConfigSchema::CONFIG_SCHEMA_FRONTEND);
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

static GstStateChangeReturn gst_hailofrontendbinsrc_v4l2src_change_state_wrapper(GstElement *element,
                                                                                 GstStateChange transition)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(gst_element_get_parent(element));
    if (self == nullptr)
    {
        GST_ERROR_OBJECT(element, "Failed to get parent element");
        return GST_STATE_CHANGE_FAILURE;
    }

    if (transition == GST_STATE_CHANGE_NULL_TO_READY)
    {
        GST_DEBUG_OBJECT(self, "v4l2src NULL_TO_READY: calling modify_isp_config_files before state change");
        self->params->m_isp_manager->modify_isp_config_files();
    }

    GstStateChangeReturn ret = g_original_v4l2src_change_state(element, transition);
    gst_object_unref(self);
    return ret;
}

static void gst_hailofrontendbinsrc_set_config(GstHailoFrontendBinSrc *self, frontend_config_t &config,
                                               std::string config_string)
{
    if (self->params->m_elements_linked &&
        self->params->m_frontend_config.input_config.resolution != config.input_config.resolution &&
        self->params->m_frontend_config.input_config.sensor_index != config.input_config.sensor_index)
    {
        GST_ERROR_OBJECT(self, "Input Video config cannot be changed while pipeline is running");
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

    if (self->params->m_config_manager_interactor)
    {
        self->params->m_config_manager_interactor->set_frontend_config(config);
    }
    if (!self->params->m_isp_manager->set_config(config))
    {
        GST_ERROR_OBJECT(self, "Failed to set ISP manager config");
        return;
    }

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
    }
    g_object_set(self->params->m_frontend, "denoise-config", &(self->params->m_frontend_config), NULL);
}

static std::optional<frontend_config_t> gst_hailofrontendbinsrc_load_config(GstHailoFrontendBinSrc *self,
                                                                            const std::string &config_string)
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Frontend config to be applied: {}",
                         config_string); // TODO: config should be handled in ConfigParser class
    if (config_string.empty())
    {
        GST_ERROR_OBJECT(self, "Config string is NULL");
        return std::nullopt;
    }

    frontend_config_t out_config;
    if (self->params->m_frontend_config_parser->config_string_to_struct<frontend_config_t>(config_string, out_config) !=
        MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to decode ISP config from json string: %s", config_string.c_str());
        return std::nullopt;
    }

    return std::make_optional(out_config);
}

bool set_dummy_profile_in_gst_mode(GstHailoFrontendBinSrc *self, const frontend_config_t &frontend_config)
{
    if (!ConfigManager::get_instance().has_interactors())
    {
        auto config_manager_interactor = ConfigManagerInteractor::create_dummy_profile_interactor(frontend_config);
        if (!config_manager_interactor.has_value())
        {
            GST_ERROR_OBJECT(self, "Failed to create dummy profile interactor");
            return false;
        }
        self->params->m_config_manager_interactor = std::move(config_manager_interactor.value());
        gst_mode_config_manager_interactor = self->params->m_config_manager_interactor.get();
        self->params->m_isp_manager->set_config_manager_interactor(self->params->m_config_manager_interactor.get(),
                                                                   false);
        g_object_set(self->params->m_frontend, "config-manager-interactor", gst_mode_config_manager_interactor, NULL);
    }
    return true;
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
        if (!set_dummy_profile_in_gst_mode(self, config.value()))
        {
            GST_ERROR_OBJECT(self, "Failed to set dummy profile in gst mode");
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
        if (!set_dummy_profile_in_gst_mode(self, config.value()))
        {
            GST_ERROR_OBJECT(self, "Failed to set dummy profile in gst mode");
            return;
        }

        // Set configurations
        gst_hailofrontendbinsrc_set_config(self, config.value(), self->params->config_string);
        break;
    }
    case PROP_CONFIG: {
        frontend_config_t *config = static_cast<frontend_config_t *>(g_value_get_pointer(value));
        if (!set_dummy_profile_in_gst_mode(self, *config))
        {
            GST_ERROR_OBJECT(self, "Failed to set dummy profile in gst mode");
            return;
        }
        g_object_set(self->params->m_frontend, "denoise-config", config, NULL);
        GST_DEBUG_OBJECT(self, "Configure Pre ISP Denoise with config struct");
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
    case PROP_CONFIG_MANAGER_INTERACTOR: {
        self->params->m_isp_manager->set_config_manager_interactor(
            static_cast<ConfigManagerInteractor *>(g_value_get_pointer(value)), true);
        g_object_set(self->params->m_frontend, "config-manager-interactor", g_value_get_pointer(value), NULL);
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
    case PROP_CONFIG_MANAGER_INTERACTOR: {
        gpointer interactor_ptr;
        interactor_ptr = gst_mode_config_manager_interactor;
        g_value_set_pointer(value, interactor_ptr);
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
        GST_DEBUG_OBJECT(self, "Stopping ISP Manager");
        self->params->m_isp_manager->stop();
        break;
    }
    case GST_STATE_CHANGE_NULL_TO_READY: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_NULL_TO_READY");
        if (self->params->m_elements_linked == FALSE)
        {
            GST_DEBUG_OBJECT(self, "Linking elements");
            gst_hailofrontendbinsrc_link_elements(GST_ELEMENT(self));
            self->params->m_elements_linked = TRUE;
        }
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
        GST_DEBUG_OBJECT(self, "GST_STATE_NULL_TO_READY");
        if (!self->params->m_pre_isp_denoise->is_enabled(self->params->m_frontend_config.denoise_config))
        {
            self->params->m_pre_isp_denoise->deinit();
        }
        break;
    }
    default:
        break;
    }

    result = GST_ELEMENT_CLASS(gst_hailofrontendbinsrc_parent_class)->change_state(element, transition);
    if (result == GST_STATE_CHANGE_FAILURE)
    {
        return result;
    }

    switch (transition)
    {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
        if (!self->params->m_isp_manager->start())
        {
            GST_ERROR_OBJECT(self, "Failed to start ISP Manager");
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL: {
        GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_NULL");
        self->params->m_pre_isp_denoise->deinit();
        self->params->m_hdr->deinit();
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
    gchar *ghostpad_name = g_strdup_printf("frontendbinsrc_ghostpad_%s", frontend_srcpad_name);
    srcpad = gst_ghost_pad_new_no_target(ghostpad_name, GST_PAD_SRC);
    const char *srcpad_name = glib_cpp::ptrs::get_pad_name(srcpad);
    g_free(ghostpad_name);
    gboolean link_status = gst_ghost_pad_set_target(GST_GHOST_PAD(srcpad.get()), frontend_srcpad);
    GST_DEBUG_OBJECT(self, "FrontendBinSrc setting %s to target %s", srcpad_name, frontend_srcpad_name);
    if (!link_status)
    {
        GST_ERROR_OBJECT(self, "FrontendBinSrc failed to set %s to target %s", srcpad_name, frontend_srcpad_name);
    }

    // Set the new ghostpad to active and add it to the bin
    gst_pad_set_active(srcpad, TRUE);
    glib_cpp::ptrs::add_pad_to_element(element, srcpad);
    // srcpad.set_auto_unref(false);
    return srcpad;
}

static void gst_hailofrontendbinsrc_release_pad(GstElement *element, GstPad *pad)
{
    GstHailoFrontendBinSrc *self = GST_HAILO_FRONTEND_BINSRC(element);
    GST_DEBUG_OBJECT(self, "Release pad: %s", glib_cpp::get_name(pad).c_str());
    // Find the corresponding source pad in GstHailoMultiResize
    GstPadPtr frontend_srcpad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    // Release the source pad in GstHailoFrontend
    gst_element_release_request_pad(self->params->m_frontend, frontend_srcpad);
    // Remove the ghost pad from GstHailoFrontendBinSrc
    glib_cpp::ptrs::remove_pad_from_element(element, pad);
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

    // Decrement refcount and unpatch when the last instance is disposed
    {
        std::lock_guard<std::mutex> lock(g_v4l2src_patch_mutex);
        if (g_v4l2src_patch_refcount > 0)
        {
            --g_v4l2src_patch_refcount;

            if (g_v4l2src_patch_refcount == 0)
            {
                GstElementClass *v4l2src_class = GST_ELEMENT_GET_CLASS(self->params->m_v4l2src);
                v4l2src_class->change_state = g_original_v4l2src_change_state;
                g_original_v4l2src_change_state = nullptr;
            }
        }
    }

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
