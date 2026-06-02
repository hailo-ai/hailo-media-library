/**
 * @file gsthailoencoder.cpp
 * @brief Implementation of the gsthailoencoder GStreamer element.
 *
 * Implements a thin GstVideoEncoder that delegates encoding to the
 * MediaLibrary backend. The element is identified by its @c stream-id
 * property, which must match a key in the active profile's
 * @c encoded_output_streams map.
 **/
#include "gsthailoencoder.hpp"

#include <gst/gst.h>
#include <gst/gsterror.h>
#include <gst/gstformat.h>
#include <gst/gstparamspecs.h>
#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <stddef.h>
#include <stdint.h>
#include <tl/expected.hpp>
#include <memory>
#include <string>
#include <map>
#include <optional>
#include <utility>
#include <variant>

#include "utils/gst_pipeline_utils.hpp"
#include "utils/medialib_instance_registry.hpp"
#include "buffer_utils.hpp"
#include "gsthailobuffermeta.hpp"
#include "gstmedialibcommon.hpp"
#include "gstmedialibptrs.hpp"
#include "media_library/media_library.hpp"
#include "buffer_pool.hpp"
#include "encoder_config_types.hpp"
#include "media_library/media_library_api_types.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailo_api_encoder_debug);
#define GST_CAT_DEFAULT gst_hailo_api_encoder_debug

struct _GstHailoApiEncoderParams
{
    std::string stream_id;
    std::string pipeline_name;

    MediaLibraryPtr medialib;

    bool subscribed = false;
    bool renegotiation_pending = false;
    bool stream_started = false;
};

namespace
{

constexpr const char *PROP_STREAM_ID_NAME = "stream-id";
constexpr int RENEG_PROCEED = 0;
constexpr int RENEG_DROP = 1;
constexpr int RENEG_UNRESOLVED = -1;

struct PtrWrapper
{
    HailoMediaLibraryBufferPtr ptr;
};

/**
 * @brief GDestroyNotify callback to release a PtrWrapper.
 * @param[in] wrapper  Pointer to the PtrWrapper to delete.
 */
static void hailo_media_library_encoder_release(PtrWrapper *wrapper)
{
    delete wrapper;
}

/**
 * @brief Wrap an encoded HailoMediaLibraryBuffer in a GstBuffer with metadata and timestamps.
 * @param[in] buffer     The encoded buffer to wrap.
 * @param[in] used_size  Number of valid bytes in the encoded output.
 * @return A new GstBuffer on success, or nullptr on failure.
 */
static GstBuffer *medialib_buffer_to_gst_buffer(HailoMediaLibraryBufferPtr buffer, uint32_t used_size)
{
    auto *wrapper = new PtrWrapper{buffer};
    GstBuffer *gst_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, buffer->get_plane_ptr(0),
                                                     buffer->get_plane_size(0), 0, used_size, wrapper,
                                                     GDestroyNotify(hailo_media_library_encoder_release));
    if (!gst_buf)
    {
        delete wrapper;
        return nullptr;
    }

    gst_buffer_add_hailo_buffer_meta(gst_buf, buffer, used_size);

    GST_BUFFER_PTS(gst_buf) = buffer->pts;
    GST_BUFFER_DTS(gst_buf) = buffer->dts;
    GST_BUFFER_DURATION(gst_buf) = buffer->duration;

    return gst_buf;
}

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264, "
                    "stream-format = (string) byte-stream, "
                    "alignment = (string) au, "
                    "profile = (string) { base, main, high };"
                    "video/x-h265, "
                    "stream-format = (string) byte-stream, "
                    "alignment = (string) au, "
                    "profile = (string) { main, main-still-picture, main-intra, main-10, main-10-intra };"
                    "image/jpeg"));

/**
 * @brief Build GstCaps from an encoder_config_t variant.
 * @param[in] enc_config  Encoder configuration (H.264, H.265, or JPEG).
 * @return GstCaps for the corresponding codec, or nullptr if unrecognized.
 */
static GstCapsPtr caps_from_encoder_config(const encoder_config_t &enc_config)
{
    if (std::holds_alternative<hailo_encoder_config_t>(enc_config))
    {
        const auto &hailo_enc_cfg = std::get<hailo_encoder_config_t>(enc_config);
        if (hailo_enc_cfg.output_stream.codec == CODEC_TYPE_H264)
        {
            return GstCapsPtr(gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream",
                                                  "alignment", G_TYPE_STRING, "au", NULL));
        }
        else
        {
            return GstCapsPtr(gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream",
                                                  "alignment", G_TYPE_STRING, "au", NULL));
        }
    }
    else if (std::holds_alternative<jpeg_encoder_config_t>(enc_config))
    {
        return GstCapsPtr(gst_caps_new_empty_simple("image/jpeg"));
    }
    return GstCapsPtr();
}

/**
 * @brief Extract the integer framerate from a GstCaps structure.
 * @param[in] caps  The caps to extract framerate from (must have at least one structure).
 * @return The framerate as an integer (fps_numerator / fps_denominator), or 0 on failure.
 */
static uint32_t get_framerate_from_caps(GstCaps *caps)
{
    if (!caps || gst_caps_get_size(caps) == 0)
        return 0;

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint fps_numerator = 0, fps_denominator = 1;
    gst_structure_get_fraction(structure, "framerate", &fps_numerator, &fps_denominator);
    return (fps_denominator > 0) ? static_cast<uint32_t>(fps_numerator / fps_denominator) : 0;
}

} // namespace

static void gst_hailo_api_encoder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_api_encoder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_hailo_api_encoder_dispose(GObject *object);

static gboolean gst_hailo_api_encoder_open(GstVideoEncoder *encoder);
static gboolean gst_hailo_api_encoder_start(GstVideoEncoder *encoder);
static gboolean gst_hailo_api_encoder_stop(GstVideoEncoder *encoder);
static GstCaps *gst_hailo_api_encoder_getcaps(GstVideoEncoder *encoder, GstCaps *filter);
static gboolean gst_hailo_api_encoder_set_format(GstVideoEncoder *encoder, GstVideoCodecState *codec_state);
static GstFlowReturn gst_hailo_api_encoder_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);

enum
{
    PROP_0,
    PROP_STREAM_ID,
};

#define gst_hailo_api_encoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoApiEncoder, gst_hailo_api_encoder, GST_TYPE_VIDEO_ENCODER,
                        GST_DEBUG_CATEGORY_INIT(gst_hailo_api_encoder_debug, "gsthailoencoder", 0,
                                                "debug category for gsthailoencoder(API) element"));

/**
 * @brief Register pads, properties, and virtual method overrides.
 * @param[in] klass  The encoder class structure.
 */
static void gst_hailo_api_encoder_class_init(GstHailoApiEncoderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class, "Hailo Encoder (MediaLibrary)", "Encoder/Video",
                                          "Thin GStreamer encoder bound to MediaLibrary by stream-id", "hailo.ai");

    gobject_class->set_property = gst_hailo_api_encoder_set_property;
    gobject_class->get_property = gst_hailo_api_encoder_get_property;
    gobject_class->dispose = gst_hailo_api_encoder_dispose;

    g_object_class_install_property(
        gobject_class, PROP_STREAM_ID,
        g_param_spec_string(PROP_STREAM_ID_NAME, "Stream id", "Mandatory stream id from profile encoded_output_streams",
                            NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    venc_class->open = gst_hailo_api_encoder_open;
    venc_class->start = gst_hailo_api_encoder_start;
    venc_class->stop = gst_hailo_api_encoder_stop;
    venc_class->getcaps = gst_hailo_api_encoder_getcaps;
    venc_class->set_format = gst_hailo_api_encoder_set_format;
    venc_class->handle_frame = gst_hailo_api_encoder_handle_frame;
}

/**
 * @brief Instance initializer — allocates GstHailoApiEncoderParams.
 * @param[in] self  The encoder instance.
 */
static void gst_hailo_api_encoder_init(GstHailoApiEncoder *self)
{
    self->params = new GstHailoApiEncoderParams();
    GST_DEBUG_OBJECT(self, "init: encoder element created");
}

/**
 * @brief GObject set_property handler for the stream-id property.
 * @param[in] object   The GObject (encoder instance).
 * @param[in] prop_id  Property identifier.
 * @param[in] value    The GValue containing the new property value.
 * @param[in] pspec    The property specification.
 */
static void gst_hailo_api_encoder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    auto *self = GST_HAILO_API_ENCODER(object);

    switch (prop_id)
    {
    case PROP_STREAM_ID: {
        const gchar *stream_id_str = g_value_get_string(value);
        if (!stream_id_str || !*stream_id_str)
        {
            GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("stream-id property value must not be null or empty"), (NULL));
            break;
        }
        self->params->stream_id = std::string(stream_id_str);
        GST_INFO_OBJECT(self, "set_property: stream-id set to '%s'", self->params->stream_id.c_str());
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/**
 * @brief GObject get_property handler for the stream-id property.
 * @param[in] object   The GObject (encoder instance).
 * @param[in] prop_id  Property identifier.
 * @param[out] value   The GValue to populate with the property value.
 * @param[in] pspec    The property specification.
 */
static void gst_hailo_api_encoder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    auto *self = GST_HAILO_API_ENCODER(object);

    switch (prop_id)
    {
    case PROP_STREAM_ID:
        g_value_set_string(value, self->params->stream_id.c_str());
        GST_DEBUG_OBJECT(self, "get_property: stream-id='%s'", self->params->stream_id.c_str());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/**
 * @brief Release MediaLibrary reference and free params on finalization.
 * @param[in] object  The GObject (encoder instance).
 */
static void gst_hailo_api_encoder_dispose(GObject *object)
{
    auto *self = GST_HAILO_API_ENCODER(object);
    GST_DEBUG_OBJECT(self, "dispose: releasing encoder resources for stream-id '%s'",
                     self->params ? self->params->stream_id.c_str() : "(null)");

    if (self->params != nullptr)
    {
        // Release MediaLibrary ref before deletion — prevents the callback
        // lambda (which captures 'self') from firing on a destroyed object.
        self->params->medialib.reset();
        self->params->subscribed = false;

        delete self->params;
        self->params = nullptr;
    }

    GST_DEBUG_OBJECT(self, "dispose: params freed");
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

/**
 * @brief Validate stream-id and resolve parent pipeline name (NULL->READY).
 * @param[in] encoder  The GstVideoEncoder instance.
 * @return TRUE on success, FALSE on error.
 */
static gboolean gst_hailo_api_encoder_open(GstVideoEncoder *encoder)
{
    auto *self = GST_HAILO_API_ENCODER(encoder);
    GST_DEBUG_OBJECT(self, "open: validating stream-id '%s'", self->params->stream_id.c_str());

    if (self->params->stream_id.empty())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("stream-id property is mandatory"), (NULL));
        return FALSE;
    }

    // Resolve pipeline name (parent bin/pipeline must exist at NULL->READY)
    auto pipeline_name_exp = hailo::gst_api::get_parent_pipeline_name(GST_ELEMENT(self));
    if (!pipeline_name_exp.has_value())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to derive parent pipeline name"), (NULL));
        return FALSE;
    }
    self->params->pipeline_name = pipeline_name_exp.value();

    GST_INFO_OBJECT(self, "open: stream-id='%s', pipeline='%s'", self->params->stream_id.c_str(),
                    self->params->pipeline_name.c_str());
    return TRUE;
}

/**
 * @brief Send stream-start, caps, and segment events on the src pad if not yet sent.
 * @param[in] self     The encoder instance.
 * @param[in] encoder  The parent GstVideoEncoder.
 * @param[in] srcpad   The src pad to push events on.
 */
static void ensure_stream_started(GstHailoApiEncoder *self, GstVideoEncoder *encoder, GstPad *srcpad)
{
    if (self->params->stream_started)
        return;

    GST_INFO_OBJECT(self, "ensure_stream_started: sending initial stream events for stream-id '%s'",
                    self->params->stream_id.c_str());

    gchar *stream_id = gst_pad_create_stream_id(srcpad, GST_ELEMENT(encoder), self->params->stream_id.c_str());
    gst_pad_push_event(srcpad, gst_event_new_stream_start(stream_id));
    g_free(stream_id);

    GstCapsPtr src_caps(gst_pad_get_current_caps(srcpad));
    if (src_caps)
        gst_pad_push_event(srcpad, gst_event_new_caps(src_caps));

    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    gst_pad_push_event(srcpad, gst_event_new_segment(&segment));

    self->params->stream_started = true;
    GST_DEBUG_OBJECT(self, "ensure_stream_started: stream started for stream-id '%s'", self->params->stream_id.c_str());
}

/**
 * @brief Wait for MediaLibrary initialization and subscribe to encoder output.
 * @param[in] self  The encoder instance.
 * @return MEDIA_LIBRARY_SUCCESS or an error code.
 */
static media_library_return gst_hailo_api_encoder_subscribe_to_output(GstHailoApiEncoder *self)
{
    GST_INFO_OBJECT(self, "Waiting for MediaLibrary (pipeline='%s', stream-id='%s')",
                    self->params->pipeline_name.c_str(), self->params->stream_id.c_str());

    // Block until vision has created and initialized the MediaLibrary.
    // If vision hasn't called create_and_initialize() yet, this will wait.
    auto medialib_exp = hailo::gst_api::MediaLibInstanceRegistry::instance().get_initialized(
        self->params->pipeline_name, self->params->stream_id);
    if (!medialib_exp.has_value())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                          ("Failed to get initialized MediaLibrary for stream-id '%s' (error=%d). "
                           "If another encoder element already uses this stream-id, only one is allowed.",
                           self->params->stream_id.c_str(), (int)medialib_exp.error()),
                          (NULL));
        return medialib_exp.error();
    }
    GST_INFO_OBJECT(self, "Obtained initialized MediaLibrary instance");
    self->params->medialib = medialib_exp.value();

    if (self->params->subscribed)
    {
        GST_DEBUG_OBJECT(self, "Already subscribed but medialib doesnt exist yet");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (!self->params->medialib)
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("MediaLibrary instance is missing"), (NULL));
        return MEDIA_LIBRARY_UNINITIALIZED;
    }

    // Validate stream-id exists in current profile
    auto profile_exp = self->params->medialib->get_current_profile();
    if (!profile_exp.has_value())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Failed to get current profile from MediaLibrary"), (NULL));
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    auto stream_it = profile_exp.value().encoded_output_streams.find(self->params->stream_id);
    if (stream_it == profile_exp.value().encoded_output_streams.end())
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                          ("stream-id '%s' not found in current profile", self->params->stream_id.c_str()), (NULL));
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    GST_DEBUG_OBJECT(self, "Stream-id '%s' validated in profile", self->params->stream_id.c_str());

    // Subscribe to encoder output — wrap encoded buffers and push to src pad
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(self);
    auto subscribe_status = self->params->medialib->subscribe_to_encoder_output(
        self->params->stream_id, [self, encoder](HailoMediaLibraryBufferPtr buffer, uint32_t used_size) {
            if (!buffer)
            {
                GST_WARNING_OBJECT(self, "output callback: received null buffer for stream-id '%s'",
                                   self->params->stream_id.c_str());
                return;
            }

            GST_DEBUG_OBJECT(self, "output callback: received encoded buffer (size=%u) for stream-id '%s'", used_size,
                             self->params->stream_id.c_str());

            GstBuffer *gst_buf = medialib_buffer_to_gst_buffer(buffer, used_size);
            if (!gst_buf)
            {
                GST_WARNING_OBJECT(self, "output callback: failed to create GstBuffer for stream-id '%s'",
                                   self->params->stream_id.c_str());
                return;
            }

            GstPad *srcpad = GST_VIDEO_ENCODER_SRC_PAD(encoder);
            ensure_stream_started(self, encoder, srcpad);
            gst_pad_push(srcpad, gst_buf);
            GST_DEBUG_OBJECT(self, "output callback: pushed encoded buffer to src pad for stream-id '%s'",
                             self->params->stream_id.c_str());
        });

    if (subscribe_status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
                          ("Failed to subscribe to encoder output (subscribe status=%d)", (int)subscribe_status),
                          (NULL));
        return subscribe_status;
    }

    self->params->subscribed = true;
    GST_INFO_OBJECT(self, "Subscribed to encoder output for stream-id '%s'", self->params->stream_id.c_str());
    return MEDIA_LIBRARY_SUCCESS;
}

/**
 * @brief Start handler — triggers deferred_init (READY->PAUSED).
 * @param[in] encoder  The GstVideoEncoder instance.
 * @return TRUE on success, FALSE on error.
 */
static gboolean gst_hailo_api_encoder_start(GstVideoEncoder *encoder)
{
    auto *self = GST_HAILO_API_ENCODER(encoder);
    GST_INFO_OBJECT(self, "start: beginning for stream-id '%s'", self->params->stream_id.c_str());

    // Ensure the MediaLibrary encoder exists and we are subscribed before we accept frames.
    auto subscribe_status = gst_hailo_api_encoder_subscribe_to_output(self);
    if (subscribe_status != MEDIA_LIBRARY_SUCCESS)
        return FALSE;

    GST_INFO_OBJECT(self, "start: complete for stream-id '%s'", self->params->stream_id.c_str());
    return TRUE;
}

/**
 * @brief Stop handler — reset local state without touching MediaLibrary lifecycle.
 * @param[in] encoder  The GstVideoEncoder instance.
 * @return TRUE always.
 */
static gboolean gst_hailo_api_encoder_stop(GstVideoEncoder *encoder)
{
    auto *self = GST_HAILO_API_ENCODER(encoder);
    GST_DEBUG_OBJECT(self, "stop: stopping encoder for stream-id '%s'", self->params->stream_id.c_str());

    // Release the stream-id claim so another element can use it after re-start.
    hailo::gst_api::MediaLibInstanceRegistry::instance().release_encoder_stream(self->params->pipeline_name,
                                                                                self->params->stream_id);

    // Reset local state for potential re-start. Do NOT touch medialib lifecycle —
    // the vision element owns start_pipeline / stop_pipeline.
    self->params->subscribed = false;
    self->params->renegotiation_pending = false;
    self->params->stream_started = false;
    self->params->medialib.reset();

    GST_INFO_OBJECT(self, "stop: encoder stopped for stream-id '%s'", self->params->stream_id.c_str());
    return TRUE;
}

/**
 * @brief Return ANY sink caps so the encoder does not constrain upstream negotiation.
 * @param[in] encoder  The GstVideoEncoder instance.
 * @param[in] filter   Optional filter caps to intersect with, or NULL.
 * @return ANY caps (intersected with filter if provided).
 */
static GstCaps *gst_hailo_api_encoder_getcaps(GstVideoEncoder *encoder, GstCaps *filter)
{
    auto *self = GST_HAILO_API_ENCODER(encoder);

    GstCapsPtr caps(gst_caps_new_any());
    GST_DEBUG_OBJECT(self, "getcaps: returning ANY caps for stream-id '%s'", self->params->stream_id.c_str());

    if (filter)
        caps.reset(gst_caps_intersect(caps, filter));

    caps.set_auto_unref(false);
    return caps;
}

/**
 * @brief Check whether an incoming buffer matches the negotiated sink caps.
 *
 * Called when renegotiation is pending after a profile switch. If the
 * buffer's attached profile matches current caps, clears the pending
 * flag. If it does not match, the caller should drop the frame.
 *
 * @param[in] self       The encoder element.
 * @param[in] encoder    The parent GstVideoEncoder.
 * @param[in] profile    The buffer's attached profile.
 * @return  0 if renegotiation is not pending or buffer matches (proceed),
 *          RENEG_DROP if buffer does not match and should be dropped,
 *          RENEG_UNRESOLVED if the stream-id or config could not be resolved (proceed anyway).
 */
static int check_renegotiation(GstHailoApiEncoder *self, GstVideoEncoder *encoder,
                               std::shared_ptr<const config_profile_t> profile)
{
    if (!self->params->renegotiation_pending)
        return RENEG_PROCEED;

    auto stream_it = profile->encoded_output_streams.find(self->params->stream_id);
    if (stream_it == profile->encoded_output_streams.end())
    {
        GST_WARNING_OBJECT(self, "check_renegotiation: stream-id '%s' not found in attached profile",
                           self->params->stream_id.c_str());
        return RENEG_UNRESOLVED;
    }

    auto buffer_input_config = hailo::gst_api::get_input_config_from_encoder(stream_it->second.encoding);
    GstCapsPtr negotiated_caps(gst_pad_get_current_caps(GST_VIDEO_ENCODER_SINK_PAD(encoder)));
    if (!buffer_input_config || !negotiated_caps)
    {
        GST_DEBUG_OBJECT(self, "check_renegotiation: could not get input config or negotiated caps");
        return RENEG_UNRESOLVED;
    }

    GstStructure *negotiated_structure = gst_caps_get_structure(negotiated_caps, 0);
    gint negotiated_width = 0, negotiated_height = 0;
    gst_structure_get_int(negotiated_structure, "width", &negotiated_width);
    gst_structure_get_int(negotiated_structure, "height", &negotiated_height);

    uint32_t negotiated_framerate = get_framerate_from_caps(negotiated_caps);

    const gchar *format_str = gst_structure_get_string(negotiated_structure, "format");
    std::string negotiated_format = format_str ? format_str : "";

    bool match =
        ((uint32_t)negotiated_width == buffer_input_config->width &&
         (uint32_t)negotiated_height == buffer_input_config->height &&
         negotiated_framerate == buffer_input_config->framerate && negotiated_format == buffer_input_config->format);

    if (match)
    {
        self->params->renegotiation_pending = false;
        GST_INFO_OBJECT(self, "check_renegotiation: buffer matches negotiated caps — renegotiation complete");
        return RENEG_PROCEED;
    }

    GST_DEBUG_OBJECT(self, "check_renegotiation: dropping buffer — negotiated %dx%d@%u %s vs profile %ux%u@%u %s",
                     negotiated_width, negotiated_height, negotiated_framerate, negotiated_format.c_str(),
                     buffer_input_config->width, buffer_input_config->height, buffer_input_config->framerate,
                     buffer_input_config->format.c_str());
    return RENEG_DROP;
}

/**
 * @brief Try to derive output caps from the current profile's encoder config.
 * @param[in] self  The encoder instance.
 * @return GstCapsPtr with codec caps, or empty if profile/stream not available.
 */
static GstCapsPtr derive_caps_from_profile(GstHailoApiEncoder *self)
{
    if (!self->params->medialib)
        return GstCapsPtr();

    auto profile_exp = self->params->medialib->get_current_profile();
    if (!profile_exp.has_value())
        return GstCapsPtr();

    auto stream_it = profile_exp.value().encoded_output_streams.find(self->params->stream_id);
    if (stream_it == profile_exp.value().encoded_output_streams.end())
        return GstCapsPtr();

    return caps_from_encoder_config(stream_it->second.encoding);
}

/**
 * @brief Derive output caps from the encoder profile, or fall back to template caps.
 * @param[in] self     The encoder instance.
 * @param[in] encoder  The GstVideoEncoder instance.
 * @return Owning GstCaps pointer (never null).
 */
static GstCapsPtr derive_output_caps(GstHailoApiEncoder *self, GstVideoEncoder *encoder)
{
    auto profile_caps = derive_caps_from_profile(self);
    if (profile_caps)
        return profile_caps;

    GST_DEBUG_OBJECT(self, "derive_output_caps: could not derive from profile, using fallback");
    GstCapsPtr allowed_caps = gst_pad_get_allowed_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder));
    if (!allowed_caps || gst_caps_is_any(allowed_caps))
        allowed_caps.reset(gst_pad_get_pad_template_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder)));
    return glib_cpp::ptrs::fixate_caps(allowed_caps);
}

/**
 * @brief Handle upstream format negotiation and set output caps.
 * @param[in] encoder      The GstVideoEncoder instance.
 * @param[in] codec_state  The negotiated input video codec state.
 * @return TRUE on success.
 */
static gboolean gst_hailo_api_encoder_set_format(GstVideoEncoder *encoder, GstVideoCodecState *codec_state)
{
    auto *self = GST_HAILO_API_ENCODER(encoder);
    GstVideoCodecStatePtr codec_state_ptr = codec_state;

    GST_DEBUG_OBJECT(self, "set_format: format negotiation for stream-id '%s'", self->params->stream_id.c_str());

    GstCapsPtr output_caps = derive_output_caps(self, encoder);
    glib_cpp::ptrs::video_encoder_set_output_state(encoder, output_caps, codec_state_ptr);

    // After caps change, next buffer may not match yet — mark pending
    self->params->renegotiation_pending = true;

    GST_INFO_OBJECT(self, "set_format: input %dx%d, output %" GST_PTR_FORMAT, GST_VIDEO_INFO_WIDTH(&codec_state->info),
                    GST_VIDEO_INFO_HEIGHT(&codec_state->info), output_caps.get());
    return TRUE;
}

/**
 * @brief Renegotiate output caps if the attached profile's codec differs from current src caps.
 *
 * handles codec changes (media type: video/x-h264 vs video/x-h265). Resolution and framerate
 * renegotiation is already handled by gsthailovision pushing new caps upstream.
 *
 * @param[in] self     The encoder element.
 * @param[in] encoder  The parent GstVideoEncoder.
 * @param[in] profile  The buffer's attached profile.
 * @return true if caps were renegotiated, false otherwise.
 */
static bool renegotiate_output_codec_if_needed(GstHailoApiEncoder *self, GstVideoEncoder *encoder,
                                               std::shared_ptr<const config_profile_t> profile)
{
    auto stream_it = profile->encoded_output_streams.find(self->params->stream_id);
    if (stream_it == profile->encoded_output_streams.end())
        return false;

    GstCapsPtr new_caps = caps_from_encoder_config(stream_it->second.encoding);
    if (!new_caps)
        return false;

    GstCapsPtr current_caps(gst_pad_get_current_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder)));
    if (!current_caps)
        return false;

    const gchar *current_media = gst_structure_get_name(gst_caps_get_structure(current_caps, 0));
    const gchar *new_media = gst_structure_get_name(gst_caps_get_structure(new_caps, 0));
    bool codec_changed = (g_strcmp0(current_media, new_media) != 0);

    if (!codec_changed)
        return false;

    GST_INFO_OBJECT(self, "Output codec changed for stream '%s': %s -> %s — renegotiating src caps",
                    self->params->stream_id.c_str(), current_media, new_media);

    GstVideoCodecStatePtr null_ref;
    glib_cpp::ptrs::video_encoder_set_output_state(encoder, new_caps, null_ref);
    gst_video_encoder_negotiate(encoder);

    return true;
}

/**
 * @brief Process an incoming video frame and route it to MediaLibrary.
 * @param[in] encoder  The GstVideoEncoder instance.
 * @param[in] frame    The video codec frame to process.
 * @return GST_FLOW_OK on success, GST_FLOW_ERROR on failure.
 */
static GstFlowReturn gst_hailo_api_encoder_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame)
{
    auto *self = GST_HAILO_API_ENCODER(encoder);
    GST_DEBUG_OBJECT(self, "handle_frame: received frame %u for stream-id '%s'", frame->system_frame_number,
                     self->params->stream_id.c_str());

    if (!self->params->medialib || !self->params->subscribed)
    {
        GST_ERROR_OBJECT(self, "handle_frame: MediaLibrary not ready (medialib=%p, subscribed=%d)",
                         self->params->medialib.get(), self->params->subscribed);
        gst_video_encoder_finish_frame(encoder, frame);
        return GST_FLOW_ERROR;
    }

    // 1. Extract HailoMediaLibraryBuffer from raw buffer or metadate
    HailoMediaLibraryBufferPtr hailo_buffer_ptr;
    GST_DEBUG_OBJECT(self, "handle_frame: no meta, reconstructing from GstBuffer");
    GstCapsPtr sink_caps(gst_pad_get_current_caps(GST_VIDEO_ENCODER_SINK_PAD(encoder)));
    hailo_buffer_ptr = hailo_buffer_from_gst_buffer(frame->input_buffer, sink_caps);

    if (!hailo_buffer_ptr)
    {
        GST_ERROR_OBJECT(self, "handle_frame: could not get hailo buffer from input");
        gst_video_encoder_finish_frame(encoder, frame);
        return GST_FLOW_ERROR;
    }

    // 2. Validate buffer's attached profile vs negotiated caps after renegotiation
    auto attached_profile = hailo_buffer_ptr->get_attached_profile();
    if (!attached_profile)
    {
        GST_ERROR_OBJECT(self, "handle_frame: buffer missing attached profile for frame %u",
                         frame->system_frame_number);
        gst_video_encoder_finish_frame(encoder, frame);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "handle_frame: attached profile resolved for frame %u", frame->system_frame_number);

    int reneg_result = check_renegotiation(self, encoder, attached_profile);
    if (reneg_result == RENEG_DROP)
    {
        return gst_video_encoder_finish_frame(encoder, frame);
    }

    renegotiate_output_codec_if_needed(self, encoder, attached_profile);

    // 3. Route buffer to MediaLibrary encoder
    GST_DEBUG_OBJECT(self, "handle_frame: sending frame %u to encoder '%s'", frame->system_frame_number,
                     self->params->stream_id.c_str());
    auto encode_status = self->params->medialib->add_buffer_to_encoder(self->params->stream_id, hailo_buffer_ptr);
    if (encode_status != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "handle_frame: add_buffer_to_encoder failed for '%s' (rc=%d)",
                         self->params->stream_id.c_str(), (int)encode_status);
        gst_video_encoder_finish_frame(encoder, frame);
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(self, "handle_frame: frame %u sent successfully to encoder '%s'", frame->system_frame_number,
                     self->params->stream_id.c_str());
    return gst_video_encoder_finish_frame(encoder, frame);
}
