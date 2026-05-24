#include "gsthailovision.hpp"

#include <gst/gst.h>
#include <gst/gstcompat.h>
#include <gst/gsterror.h>
#include <gst/gstformat.h>
#include <gst/gstparamspecs.h>
#include <stddef.h>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include <string>
#include <unordered_map>
#include <exception>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "utils/buffer_forwarder.hpp"
#include "utils/gst_pipeline_utils.hpp"
#include "utils/medialib_instance_registry.hpp"
#include "gstmedialibcommon.hpp"
#include "gstmedialibptrs.hpp"
#include "media_library/media_library.hpp"
#include "encoder_config_types.hpp"
#include "media_library/media_library_api_types.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailo_vision_debug);
#define GST_CAT_DEFAULT gst_hailo_vision_debug

// Load config JSON from file path, returns empty string on failure
static std::string load_config_from_file(GstHailoVision *self, const gchar *path)
{
    try
    {
        GST_INFO_OBJECT(self, "Reading config from file: %s", path);
        std::string json_str = gstmedialibcommon::read_json_string_from_file(std::string(path));
        return nlohmann::json::parse(json_str).dump();
    }
    catch (const std::exception &e)
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to read/parse config JSON from '%s'", path),
                          ("%s", e.what()));
        return {};
    }
}

struct SrcPadState
{
    GstPad *pad = nullptr; // owned by element
    std::string stream_id;
};

struct _GstHailoVisionParams
{
    std::string config_string;
    std::string pipeline_name;

    MediaLibraryPtr medialib;

    // stream-id → srcpad state
    std::unordered_map<std::string, SrcPadState> srcpads_by_stream_id;

    // stream-id → buffer forwarder (queue + worker thread)
    std::unordered_map<std::string, std::unique_ptr<BufferForwarder>> forwarders;

    std::string profile_name;
};

static GstCapsPtr gst_hailo_vision_caps_from_input_config(const input_config_t &input_cfg)
{
    std::string gst_format;
    if (input_cfg.format == "NV12" || input_cfg.format.empty())
        gst_format = "NV12";
    else
    {
        GST_ERROR("Unsupported format in input config: '%s'", input_cfg.format.c_str());
        return nullptr;
    }

    if (input_cfg.framerate <= 0)
    {
        GST_ERROR("Invalid framerate in input config: %u", input_cfg.framerate);
        return nullptr;
    }

    return gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, gst_format.c_str(), "width", G_TYPE_INT,
                               (gint)input_cfg.width, "height", G_TYPE_INT, (gint)input_cfg.height, "framerate",
                               GST_TYPE_FRACTION, (gint)input_cfg.framerate, 1, NULL);
}

static void gst_hailo_vision_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_vision_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_hailo_vision_change_state(GstElement *element, GstStateChange transition);
static void gst_hailo_vision_dispose(GObject *object);
static GstPad *gst_hailo_vision_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                const GstCaps *caps);
static void gst_hailo_vision_release_pad(GstElement *element, GstPad *pad);
static void push_renegotiated_caps_if_needed(GstHailoVision *self);

enum
{
    PROP_0,
    PROP_CONFIG_STRING,
    PROP_CONFIG_PATH,
    PROP_PROFILE,
    PROP_CURRENT_PROFILE,
    PROP_OVERRIDE_PROFILE,
};

namespace
{
constexpr const char *PROP_CONFIG_STRING_NAME = "config-string";
constexpr const char *PROP_CONFIG_PATH_NAME = "config-path";
constexpr const char *PROP_PROFILE_NAME = "profile-name";
constexpr const char *PROP_CURRENT_PROFILE_NAME = "current-profile";
constexpr const char *PROP_OVERRIDE_PROFILE_NAME = "override-profile";

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("%s", GST_PAD_SRC, GST_PAD_REQUEST, GST_STATIC_CAPS_ANY);
} // namespace

#define gst_hailo_vision_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoVision, gst_hailo_vision, GST_TYPE_ELEMENT,
                        GST_DEBUG_CATEGORY_INIT(gst_hailo_vision_debug, "hailovision", 0,
                                                "debug category for hailovision element"));

static void gst_hailo_vision_class_init(GstHailoVisionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_set_static_metadata(element_class, "Hailo Vision (MediaLibrary source)", "Source/Video",
                                          "Multi-output source element wrapping MediaLibrary frontend", "hailo.ai");

    gobject_class->set_property = gst_hailo_vision_set_property;
    gobject_class->get_property = gst_hailo_vision_get_property;
    gobject_class->dispose = gst_hailo_vision_dispose;

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_STRING,
        g_param_spec_string(PROP_CONFIG_STRING_NAME, "Config string", "MediaLibrary JSON config string", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG_PATH,
        g_param_spec_string(PROP_CONFIG_PATH_NAME, "Config path", "Path to MediaLibrary JSON config file", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        gobject_class, PROP_PROFILE,
        g_param_spec_string(PROP_PROFILE_NAME, "Profile name", "Profile name to set (user-triggered)", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_CURRENT_PROFILE,
        g_param_spec_pointer(
            PROP_CURRENT_PROFILE_NAME, "Current profile",
            "Pointer to config_profile_t of the active profile (read-only, caller owns and must delete)",
            (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OVERRIDE_PROFILE,
        g_param_spec_pointer(PROP_OVERRIDE_PROFILE_NAME, "Override profile",
                             "Pointer to config_profile_t to apply via set_override_parameters (write-only)",
                             (GParamFlags)(G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailo_vision_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_hailo_vision_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_hailo_vision_release_pad);
}

static void gst_hailo_vision_init(GstHailoVision *self)
{
    self->params = new GstHailoVisionParams();
}

static void gst_hailo_vision_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    auto *self = GST_HAILO_VISION(object);

    switch (prop_id)
    {
    case PROP_CONFIG_STRING: {
        if (!self->params->config_string.empty())
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Cannot set config string when config is already set"), (NULL));
            break;
        }
        const gchar *config_string_value = g_value_get_string(value);
        if (!config_string_value)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Cannot config empty string config"), (NULL));
        }
        self->params->config_string = std::string(config_string_value);
        GST_INFO_OBJECT(self, "Config string set");
        if (!hailo::gst_api::looks_like_json(self->params->config_string))
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                              ("Config string does not look like valid JSON. Breaking without setting the config."),
                              (NULL));
        }
        break;
    }
    case PROP_CONFIG_PATH: {
        const gchar *config_path_value = g_value_get_string(value);
        if (!config_path_value || !*config_path_value)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Config path cannot be empty"), (NULL));
            break;
        }
        if (!self->params->config_string.empty())
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                              ("Cannot set config path when config is already set. Breaking without setting the path"),
                              (NULL));
            break;
        }
        self->params->config_string = load_config_from_file(self, config_path_value);
        break;
    }
    case PROP_PROFILE: {
        const gchar *profile_name_value = g_value_get_string(value);
        if (!profile_name_value)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Profile name empty"), (NULL));
            break;
        }
        self->params->profile_name = std::string(profile_name_value);

        if (!self->params->medialib || self->params->medialib->get_pipeline_state() ==
                                           media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                              ("MediaLibrary not yet initialized, profile can be changed after pipeline starts"),
                              (NULL));
            break;
        }

        auto status = self->params->medialib->set_profile(self->params->profile_name);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                              ("Failed to set profile '%s'", self->params->profile_name.c_str()),
                              ("MediaLibrary::set_profile returned %d", (int)status));
        }
        else
        {
            push_renegotiated_caps_if_needed(self);
        }
        break;
    }
    case PROP_OVERRIDE_PROFILE: {
        const auto *profile = static_cast<const config_profile_t *>(g_value_get_pointer(value));
        if (!profile)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("override-profile: null pointer"), (NULL));
            break;
        }
        if (!self->params->medialib || self->params->medialib->get_pipeline_state() ==
                                           media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("override-profile: MediaLibrary not yet initialized"), (NULL));
            break;
        }
        auto status = self->params->medialib->set_override_parameters(*profile);
        if (status != MEDIA_LIBRARY_SUCCESS)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                              ("override-profile: set_override_parameters failed (rc=%d)", (int)status), (NULL));
            break;
        }
        push_renegotiated_caps_if_needed(self);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_hailo_vision_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    auto *self = GST_HAILO_VISION(object);

    switch (prop_id)
    {
    case PROP_CONFIG_STRING:
        g_value_set_string(value, self->params->config_string.c_str());
        break;
    case PROP_PROFILE:
        g_value_set_string(value, self->params->profile_name.c_str());
        break;
    case PROP_CURRENT_PROFILE: {
        if (self->params->medialib && self->params->medialib->get_pipeline_state() !=
                                          media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
        {
            auto profile_exp = self->params->medialib->get_current_profile();
            if (profile_exp.has_value())
            {
                g_value_set_pointer(value, new config_profile_t(profile_exp.value()));
            }
            else
            {
                g_value_set_pointer(value, nullptr);
            }
        }
        else
        {
            g_value_set_pointer(value, nullptr);
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstPad *gst_hailo_vision_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                const GstCaps *)
{
    auto *self = GST_HAILO_VISION(element);

    if (!name)
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                          ("Pad name is required (e.g. sink0). Use: gsthailovision name=v ... v.<stream-id>"), (NULL));
        return nullptr;
    }

    std::string stream_id = hailo::gst_api::stream_id_from_pad_name(name);
    if (stream_id.empty())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Could not extract stream-id from pad name '%s'. ", name), (NULL));
        return nullptr;
    }

    if (self->params->srcpads_by_stream_id.count(stream_id))
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Pad for stream-id '%s' already exists", stream_id.c_str()), (NULL));
        return nullptr;
    }

    // Validate stream-id against frontend output streams if medialib is already initialized
    if (self->params->medialib)
    {
        auto outputs_exp = self->params->medialib->get_frontend_output_streams();
        if (outputs_exp.has_value())
        {
            const auto &outputs = outputs_exp.value();
            bool found = false;
            std::string available;
            for (const auto &out : outputs)
            {
                if (out.id == stream_id)
                    found = true;
                if (!available.empty())
                    available += ", ";
                available += out.id;
            }
            if (!found)
            {
                GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                                  ("stream-id '%s' not found in frontend output streams. Available: %s",
                                   stream_id.c_str(), available.c_str()),
                                  (NULL));
                return nullptr;
            }
        }
    }

    // Create the pad
    GstPadPtr srcpad;
    GST_OBJECT_LOCK(self);
    srcpad = gst_pad_new_from_template(templ, name);
    GST_OBJECT_UNLOCK(self);

    if (!srcpad)
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to create pad '%s'", name), (NULL));
        return nullptr;
    }

    gst_pad_set_active(srcpad, TRUE);
    glib_cpp::ptrs::add_pad_to_element(GST_ELEMENT(self), srcpad);

    // NOTE: Do NOT set caps here — caps must come after stream-start event.
    // Caps will be set in subscribe_and_start() in the correct order:
    // stream-start → caps → segment → buffers

    // Store in map — pad is now owned by the element, disable auto-unref
    GstPad *raw_pad = srcpad.get();
    srcpad.set_auto_unref(false);

    SrcPadState state;
    state.pad = raw_pad;
    state.stream_id = stream_id;
    self->params->srcpads_by_stream_id[stream_id] = state;

    GST_INFO_OBJECT(self, "Created srcpad '%s' for stream-id '%s'", name, stream_id.c_str());
    return raw_pad;
}

static void gst_hailo_vision_release_pad(GstElement *element, GstPad *pad)
{
    auto *self = GST_HAILO_VISION(element);
    auto pad_name_str = glib_cpp::get_name(pad);
    std::string stream_id = hailo::gst_api::stream_id_from_pad_name(pad_name_str.c_str());

    GST_INFO_OBJECT(self, "Releasing pad '%s' (stream-id '%s')", pad_name_str.c_str(), stream_id.c_str());

    if (self->params)
    {
        self->params->srcpads_by_stream_id.erase(stream_id);
    }

    gst_pad_set_active(pad, FALSE);
    glib_cpp::ptrs::remove_pad_from_element(GST_ELEMENT(self), pad);
}

// After a profile switch, compare each srcpad's current caps against the caps derived from the
// new profile.  If they differ (e.g. resolution or fps changed), push a new CAPS event so
// downstream elements can re-negotiate.  Events that don't affect caps (dewarp, grayscale, etc.)
// leave the caps unchanged and are silently ignored here.
static void push_renegotiated_caps_if_needed(GstHailoVision *self)
{
    if (!self->params->medialib ||
        self->params->medialib->get_pipeline_state() != media_library_pipeline_state_t::PIPELINE_STATE_RUNNING)
        return;

    auto profile_exp = self->params->medialib->get_current_profile();
    if (!profile_exp.has_value())
        return;
    const auto &profile = profile_exp.value();

    for (auto &[stream_id, pad_state] : self->params->srcpads_by_stream_id)
    {
        auto it = profile.encoded_output_streams.find(stream_id);
        if (it == profile.encoded_output_streams.end())
        {
            GST_ERROR_OBJECT(self,
                             "Stream-id '%s' not found in current profile after profile switch. This should never "
                             "happen since we validate against the profile before creating pads.",
                             stream_id.c_str());
            continue;
        }

        auto input_cfg = hailo::gst_api::get_input_config_from_encoder(it->second.encoding);
        if (!input_cfg)
        {
            GST_ERROR_OBJECT(self, "Failed to get input config from encoder config for stream '%s'", stream_id.c_str());
            continue;
        }

        GstCapsPtr new_caps = gst_hailo_vision_caps_from_input_config(*input_cfg);
        if (!new_caps)
        {
            GST_ERROR_OBJECT(self, "Failed to create caps from input config for stream '%s'", stream_id.c_str());
            continue;
        }

        GstCaps *current_caps = gst_pad_get_current_caps(pad_state.pad);
        bool caps_changed = !current_caps || !gst_caps_is_equal(current_caps, new_caps.get());
        if (current_caps)
            gst_caps_unref(current_caps);

        if (caps_changed)
        {
            GST_INFO_OBJECT(self, "Caps changed for stream '%s' after profile switch — re-negotiating",
                            stream_id.c_str());
            if (!gst_pad_set_caps(pad_state.pad, new_caps.get()))
            {
                GST_WARNING_OBJECT(self, "Failed to push new caps for stream '%s'", stream_id.c_str());
            }
        }
    }
}

static bool send_initial_events_on_srcpads(GstHailoVision *self, const config_profile_t &profile)
{
    for (auto &[stream_id, pad_state] : self->params->srcpads_by_stream_id)
    {
        // 1. Stream-start event
        gchar *stream_id_str =
            g_strdup_printf("hailovision/%s/%s", self->params->pipeline_name.c_str(), stream_id.c_str());
        GstEvent *stream_start = gst_event_new_stream_start(stream_id_str);
        g_free(stream_id_str);
        if (!gst_pad_push_event(pad_state.pad, stream_start))
        {
            GST_ERROR_OBJECT(self, "Failed to push stream-start event for stream-id '%s'", stream_id.c_str());
            return false;
        }

        // 2. Caps event (must come after stream-start)
        const encoder_config_t &enc_config = profile.encoded_output_streams.at(stream_id).encoding;
        auto input_cfg = hailo::gst_api::get_input_config_from_encoder(enc_config);
        if (!input_cfg)
        {
            GST_ERROR_OBJECT(self, "encoder config is not valid");
            return false;
        }
        GstCapsPtr caps = gst_hailo_vision_caps_from_input_config(*input_cfg);
        if (!caps)
        {
            GST_ERROR_OBJECT(self, "Invalid input config for stream-id '%s' — unsupported format or framerate",
                             stream_id.c_str());
            return false;
        }
        GST_INFO_OBJECT(self, "Caps for stream-id '%s': %" GST_PTR_FORMAT, stream_id.c_str(), caps.get());
        gst_pad_set_caps(pad_state.pad, caps);

        // 3. Segment event (must come after caps)
        GstSegment segment;
        gst_segment_init(&segment, GST_FORMAT_TIME);
        GstEvent *segment_event = gst_event_new_segment(&segment);
        if (!gst_pad_push_event(pad_state.pad, segment_event))
        {
            GST_WARNING_OBJECT(self, "Failed to push segment event for stream-id '%s'", stream_id.c_str());
        }
    }
    return true;
}

static bool validate_srcpads_against_profile(GstHailoVision *self, const config_profile_t &profile)
{
    // Check all requested stream-ids exist in profile
    for (const auto &[stream_id, _] : self->params->srcpads_by_stream_id)
    {
        if (profile.encoded_output_streams.find(stream_id) == profile.encoded_output_streams.end())
        {
            GST_ERROR_OBJECT(self, "stream-id '%s' not found in current profile. Available: %s", stream_id.c_str(),
                             hailo::gst_api::format_available_streams_ids(profile).c_str());
            return false;
        }
    }

    // Log profile streams with no srcpad as errors
    for (const auto &[profile_stream_id, _] : profile.encoded_output_streams)
    {
        if (self->params->srcpads_by_stream_id.find(profile_stream_id) == self->params->srcpads_by_stream_id.end())
        {
            GST_ERROR_OBJECT(self, "Profile stream-id '%s' has no corresponding srcpad (unused)",
                             profile_stream_id.c_str());
        }
    }

    return true;
}

static bool subscribe_to_frontend_outputs(GstHailoVision *self)
{
    auto outputs_exp = self->params->medialib->get_frontend_output_streams();
    if (!outputs_exp.has_value())
    {
        GST_ERROR_OBJECT(self, "Failed to get frontend output streams");
        return false;
    }

    FrontendGstBufferCallbacksMap gst_callbacks;
    for (const auto &out : outputs_exp.value())
    {
        auto it = self->params->srcpads_by_stream_id.find(out.id);
        if (it == self->params->srcpads_by_stream_id.end())
            continue;

        GstPad *target_pad = it->second.pad;
        std::string stream_id = it->second.stream_id;
        GST_INFO_OBJECT(self, "Creating BufferForwarder for stream-id '%s'", stream_id.c_str());

        auto forwarder = std::make_unique<BufferForwarder>(target_pad, stream_id, GST_ELEMENT(self), /*max_size=*/2);
        forwarder->start();
        self->params->forwarders[stream_id] = std::move(forwarder);

        BufferForwarder *forwarder_ptr = self->params->forwarders[stream_id].get();
        gst_callbacks[out.id] = [forwarder_ptr](GstBuffer *buffer) { forwarder_ptr->enqueue(buffer); };
    }

    auto sub_rc = self->params->medialib->subscribe_to_frontend_gst_output(gst_callbacks);
    if (sub_rc != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to subscribe to frontend output (rc=%d)", (int)sub_rc);
        return false;
    }

    return true;
}

static GstStateChangeReturn gst_hailo_vision_initialize(GstHailoVision *self)
{
    auto pipeline_name_exp = hailo::gst_api::get_parent_pipeline_name(GST_ELEMENT(self));
    if (!pipeline_name_exp.has_value())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to derive parent pipeline name"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
    }
    self->params->pipeline_name = pipeline_name_exp.value();
    GST_DEBUG_OBJECT(self, "Pipeline name: %s", self->params->pipeline_name.c_str());

    if (self->params->config_string.empty())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                          ("Either config-string or config-path must be set before going to READY"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
    }

    if (self->params->medialib)
    {
        GST_INFO_OBJECT(self, "Releasing previous MediaLibrary instance before re-initialization");
        self->params->medialib.reset();
    }

    auto medialib_exp = hailo::gst_api::MediaLibInstanceRegistry::instance().create_and_initialize(
        self->params->pipeline_name, self->params->config_string);
    if (!medialib_exp.has_value())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                          ("Failed to create/initialize MediaLibrary (rc=%d)", (int)medialib_exp.error()), (NULL));
        return GST_STATE_CHANGE_FAILURE;
    }
    self->params->medialib = medialib_exp.value();
    GST_DEBUG_OBJECT(self, "MediaLibrary initialized successfully");
    return GST_STATE_CHANGE_SUCCESS;
}

static gboolean gst_hailo_vision_subscribe_and_start(GstHailoVision *self)
{
    if (!self->params->medialib)
    {
        GST_ERROR_OBJECT(self, "MediaLibrary is not initialized");
        return FALSE;
    }

    if (self->params->medialib->get_pipeline_state() == media_library_pipeline_state_t::PIPELINE_STATE_UNINITIALIZED)
    {
        GST_ERROR_OBJECT(self, "MediaLibrary pipeline is not configured");
        return FALSE;
    }

    if (self->params->srcpads_by_stream_id.empty())
    {
        GST_ERROR_OBJECT(self, "No srcpads requested — add at least one <stream-id> pad");
        return FALSE;
    }

    auto profile_exp = self->params->medialib->get_current_profile();
    if (!profile_exp.has_value())
    {
        GST_ERROR_OBJECT(self, "Failed to get current profile");
        return FALSE;
    }
    const auto &profile = profile_exp.value();

    if (!validate_srcpads_against_profile(self, profile))
    {
        GST_ERROR_OBJECT(self, "Srcpads validation against profile failed");
        return FALSE;
    }

    if (!send_initial_events_on_srcpads(self, profile))
    {
        GST_ERROR_OBJECT(self, "Failed to send initial events on srcpads");
        return FALSE;
    }

    if (!subscribe_to_frontend_outputs(self))
    {
        GST_ERROR_OBJECT(self, "Failed to subscribe to frontend outputs");
        return FALSE;
    }

    // Subscribe to MediaLibrary internal pipeline state changes so caps are
    // renegotiated after any internal restart (thermal throttling, etc.)
    // and the first buffer after restart is marked as DISCONT.
    auto state_rc =
        self->params->medialib->subscribe_to_pipeline_state_change([self](media_library_pipeline_state_t state) {
            switch (state)
            {
            case media_library_pipeline_state_t::PIPELINE_STATE_RUNNING:
                GST_INFO_OBJECT(self, "MediaLibrary pipeline state -> RUNNING");
                push_renegotiated_caps_if_needed(self);
                break;
            case media_library_pipeline_state_t::PIPELINE_STATE_STOPPED:
                GST_WARNING_OBJECT(self, "MediaLibrary pipeline state -> STOPPED");
                break;
            default:
                break;
            }
        });
    if (state_rc != MEDIA_LIBRARY_SUCCESS)
    {
        GST_WARNING_OBJECT(self, "Failed to subscribe to pipeline state change (rc=%d)", (int)state_rc);
    }

    auto start_rc = self->params->medialib->start_pipeline();
    if (start_rc != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "Failed to start MediaLibrary pipeline (rc=%d)", (int)start_rc);
        return FALSE;
    }

    return TRUE;
}

static void gst_hailo_vision_stop(GstHailoVision *self)
{
    GST_DEBUG_OBJECT(self, "Stopping MediaLibrary pipeline");

    // Stop forwarder worker threads before stopping the pipeline.
    for (auto &[stream_id, forwarder] : self->params->forwarders)
    {
        forwarder->stop();
    }
    self->params->forwarders.clear();

    if (self->params->medialib &&
        self->params->medialib->get_pipeline_state() == media_library_pipeline_state_t::PIPELINE_STATE_RUNNING)
    {
        self->params->medialib->stop_pipeline();
    }
}

static void gst_hailo_vision_cleanup(GstHailoVision *self)
{
    GST_DEBUG_OBJECT(self, "Cleaning up MediaLibrary resources");
    if (self->params->medialib)
    {
        if (!self->params->pipeline_name.empty())
        {
            hailo::gst_api::MediaLibInstanceRegistry::instance().unregister_instance(self->params->pipeline_name);
        }
    }
}

static GstStateChangeReturn gst_hailo_vision_change_state(GstElement *element, GstStateChange transition)
{
    auto *self = GST_HAILO_VISION(element);

    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY: {
        auto result = gst_hailo_vision_initialize(self);
        if (result != GST_STATE_CHANGE_SUCCESS)
            return result;
        break;
    }
    default:
        break;
    }

    GstStateChangeReturn result = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_READY_TO_PAUSED: {
        if (result == GST_STATE_CHANGE_FAILURE)
            break;
        g_assert(self->params->medialib->get_pipeline_state() !=
                 media_library_pipeline_state_t::PIPELINE_STATE_RUNNING);
        if (!gst_hailo_vision_subscribe_and_start(self))
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to subscribe/start MediaLibrary pipeline"), (NULL));
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_hailo_vision_stop(self);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        gst_hailo_vision_cleanup(self);
        break;
    default:
        break;
    }

    return result;
}

static void gst_hailo_vision_dispose(GObject *object)
{
    auto *self = GST_HAILO_VISION(object);

    // Must call parent dispose FIRST — it releases request pads which calls release_pad,
    // which needs self->params to still be alive.
    G_OBJECT_CLASS(parent_class)->dispose(object);

    if (self->params != nullptr)
    {
        delete self->params;
        self->params = nullptr;
    }
}
