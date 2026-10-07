/*
 * Copyright (c) 2017-2026 Hailo Technologies Ltd. All rights reserved.
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
 */

#include "gsthailojpeg.hpp"

#include <gst/gst.h>
#include <gst/gstparamspecs.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <jpeglib.h>
#include <stddef.h>

#include <tl/expected.hpp>
#include <utility>
#include <variant>
#include <vector>

#include "buffer_utils.hpp"
#include "gstmedialibptrs.hpp"
#include "jpeg_encoder.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/encoder_config_types.hpp"
#include "media_library_types.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_hailojpegenc_debug_category);
#define GST_CAT_DEFAULT gst_hailojpegenc_debug_category

#define gst_hailojpegenc_parent_class parent_class

static void gst_hailojpegenc_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailojpegenc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_hailojpegenc_dispose(GObject *object);
static gboolean gst_hailojpegenc_start(GstVideoEncoder *encoder);
static gboolean gst_hailojpegenc_stop(GstVideoEncoder *encoder);
static gboolean gst_hailojpegenc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_hailojpegenc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);
static void apply_user_config(GstHailoJpegEnc *self, const jpeg_encoder_config_t &new_config);

GType gst_idct_method_get_type(void)
{
    static GType idct_method_type = 0;
    static const GEnumValue idct_method[] = {
        {JDCT_ISLOW, "Slow but accurate integer algorithm", "islow"},
        {JDCT_IFAST, "Faster, less accurate integer method", "ifast"},
        {JDCT_FLOAT, "Floating-point: accurate, fast on fast HW", "float"},
        {0, NULL, NULL},
    };

    if (!idct_method_type)
    {
        idct_method_type = g_enum_register_static("GstHailoIDCTMethod", idct_method);
    }
    return idct_method_type;
}

enum
{
    PROP_0,
    PROP_STREAM_ID,
    PROP_MIN_FORCE_KEY_UNIT_INTERVAL,
    PROP_IDCT_METHOD,
    PROP_CONFIG,
    PROP_USER_CONFIG,
};

#define _do_init GST_DEBUG_CATEGORY_INIT(gst_hailojpegenc_debug_category, "hailojpegenc", 0, "hailojpegenc element");
G_DEFINE_TYPE_WITH_CODE(GstHailoJpegEnc, gst_hailojpegenc, GST_TYPE_VIDEO_ENCODER, _do_init);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("image/jpeg, width = (int) [ 1, 65535 ], height = (int) [ 1, 65535 ], "
                    "framerate = (fraction) [ 0/1, MAX ], sof-marker = (int) { 0, 1, 2, 4, 9 }"));

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("NV12")));

static void gst_hailojpegenc_class_init(GstHailoJpegEncClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS(klass);

    gobject_class->set_property = gst_hailojpegenc_set_property;
    gobject_class->get_property = gst_hailojpegenc_get_property;
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_hailojpegenc_dispose);

    g_object_class_install_property(
        gobject_class, PROP_STREAM_ID,
        g_param_spec_string("stream-id", "Stream id", "Stream id string", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_MIN_FORCE_KEY_UNIT_INTERVAL,
        g_param_spec_uint64("min-force-key-unit-interval", "Minimum Force Keyunit Interval",
                            "Minimum interval between force-keyunit requests in nanoseconds", 0, G_MAXUINT64,
                            DEFAULT_MIN_FORCE_KEY_UNIT_INTERVAL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_IDCT_METHOD,
                                    g_param_spec_enum("idct-method", "IDCT Method", "The IDCT algorithm to use",
                                                      GST_TYPE_IDCT_METHOD, JPEG_DEFAULT_IDCT_METHOD,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIG,
        g_param_spec_pointer("config", "Encoder config", "Encoder config as encoder_config_t",
                             (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        gobject_class, PROP_USER_CONFIG,
        g_param_spec_pointer("user-config", "Encoder user config",
                             "Encoder user config, used for setting the encoder configuration",
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_set_static_metadata(element_class, "Hailo JPEG encoder", "Codec/Encoder/Image",
                                          "Single-element JPEG encoder backed by JpegEncoder (libjpeg)",
                                          "hailo.ai <contact@hailo.ai>");

    venc_class->start = GST_DEBUG_FUNCPTR(gst_hailojpegenc_start);
    venc_class->stop = GST_DEBUG_FUNCPTR(gst_hailojpegenc_stop);
    venc_class->set_format = GST_DEBUG_FUNCPTR(gst_hailojpegenc_set_format);
    venc_class->handle_frame = GST_DEBUG_FUNCPTR(gst_hailojpegenc_handle_frame);
}

static void gst_hailojpegenc_init(GstHailoJpegEnc *self)
{
    self->params = new GstHailoJpegEncParams();
    self->params->jpeg_encoder = std::make_unique<JpegEncoder>();
    jpeg_encoder_config_t initial{};
    initial.quality = JPEG_DEFAULT_QUALITY;
    initial.n_threads = DEFAULT_NUM_OF_THREADS;
    self->params->jpeg_encoder->configure(initial);
}

static void gst_hailojpegenc_dispose(GObject *object)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(object);
    if (self->params != nullptr)
    {
        self->params->jpeg_encoder.reset();
        delete self->params;
        self->params = nullptr;
    }
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void apply_user_config(GstHailoJpegEnc *self, const jpeg_encoder_config_t &new_config)
{
    self->params->jpeg_quality = static_cast<int>(new_config.quality);
    if (new_config.n_threads != 0)
    {
        self->params->num_of_threads = new_config.n_threads;
    }
    self->params->jpeg_encoder->configure(new_config);
}

static void gst_hailojpegenc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(object);

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr))
    {
        g_error("set_property got null parameter!");
        return;
    }

    switch (prop_id)
    {
    case PROP_STREAM_ID: {
        const gchar *str = g_value_get_string(value);
        self->params->stream_id = str ? str : std::string();
        break;
    }
    case PROP_MIN_FORCE_KEY_UNIT_INTERVAL:
        self->params->jpegenc_min_force_key_unit_interval = g_value_get_uint64(value);
        break;
    case PROP_IDCT_METHOD:
        self->params->jpeg_idct_method = g_value_get_enum(value);
        break;
    case PROP_USER_CONFIG: {
        encoder_config_t *cfg = static_cast<encoder_config_t *>(g_value_get_pointer(value));
        if (cfg != nullptr)
        {
            self->params->encoder_user_config = *cfg;
            if (std::holds_alternative<jpeg_encoder_config_t>(*cfg))
            {
                apply_user_config(self, std::get<jpeg_encoder_config_t>(*cfg));
            }
            else
            {
                GST_WARNING_OBJECT(self, "user-config is not a jpeg_encoder_config_t variant; ignored");
            }
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_hailojpegenc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(object);

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr))
    {
        g_error("get_property got null parameter!");
        return;
    }

    switch (property_id)
    {
    case PROP_STREAM_ID:
        g_value_set_string(value, self->params->stream_id.c_str());
        break;
    case PROP_MIN_FORCE_KEY_UNIT_INTERVAL:
        g_value_set_uint64(value, self->params->jpegenc_min_force_key_unit_interval);
        break;
    case PROP_IDCT_METHOD:
        g_value_set_enum(value, self->params->jpeg_idct_method);
        break;
    case PROP_CONFIG:
        g_value_set_pointer(value, &self->params->encoder_user_config);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstBuffer *wrap_encoded_bitstream(GstHailoJpegEnc *self, JpegEncoder::EncodedFrame &out)
{
    auto *owned_bitstream = new std::vector<uint8_t>(std::move(out.bitstream));
    const size_t bitstream_size = owned_bitstream->size();
    GstBuffer *output_buffer = gst_buffer_new_wrapped_full(
        static_cast<GstMemoryFlags>(0), owned_bitstream->data(), bitstream_size, 0, bitstream_size, owned_bitstream,
        [](gpointer p) { delete static_cast<std::vector<uint8_t> *>(p); });
    if (output_buffer == nullptr)
    {
        delete owned_bitstream;
        GST_ERROR_OBJECT(self, "Failed to wrap JPEG bitstream (%zu bytes) in GstBuffer", bitstream_size);
    }
    return output_buffer;
}

static void completion_thread_loop(GstHailoJpegEnc *self)
{
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(self);
    auto *params = self->params;
    while (true)
    {
        GstVideoCodecFrame *frame = nullptr;
        {
            std::unique_lock<std::mutex> lock(params->pending_mtx);
            params->pending_cv.wait(
                lock, [params]() { return params->stop_completion.load() || !params->pending_frames.empty(); });
            if (params->pending_frames.empty())
            {
                return;
            }
            frame = params->pending_frames.front();
            params->pending_frames.pop();
        }

        auto encoded = params->jpeg_encoder->dequeue();
        if (!encoded.has_value())
        {
            GST_ERROR_OBJECT(self, "JpegEncoder::dequeue returned %d; dropping frame",
                             static_cast<int>(encoded.error()));
            frame->output_buffer = nullptr;
            gst_video_encoder_finish_frame(encoder, frame);
            continue;
        }

        GstBuffer *output_buffer = wrap_encoded_bitstream(self, encoded.value());
        if (output_buffer == nullptr)
        {
            frame->output_buffer = nullptr;
            gst_video_encoder_finish_frame(encoder, frame);
            continue;
        }
        frame->output_buffer = output_buffer;
        gst_video_encoder_finish_frame(encoder, frame);
    }
}

static gboolean gst_hailojpegenc_start(GstVideoEncoder *encoder)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(encoder);
    GST_INFO_OBJECT(self, "hailojpegenc start (stream-id='%s')", self->params->stream_id.c_str());

    jpeg_encoder_config_t cfg{};
    cfg.quality = static_cast<uint32_t>(self->params->jpeg_quality);
    cfg.n_threads = self->params->num_of_threads;
    self->params->jpeg_encoder->configure(cfg);

    self->params->stop_completion.store(false);
    self->params->completion_thread = std::thread(completion_thread_loop, self);
    return TRUE;
}

static gboolean gst_hailojpegenc_stop(GstVideoEncoder *encoder)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(encoder);
    GST_INFO_OBJECT(self, "hailojpegenc stop");
    {
        std::lock_guard<std::mutex> lock(self->params->pending_mtx);
        self->params->stop_completion.store(true);
    }
    self->params->pending_cv.notify_all();

    // Stop the encoder's worker pool BEFORE joining the completion thread. If the
    // completion thread is parked inside JpegEncoder::dequeue() waiting for an in-flight
    // frame's done flag, only the worker can wake it -- and the worker may take an
    // arbitrary time to finish (or never, in pathological cases). stop_workers() forces
    // the wake by setting m_stop and notifying m_out_ready, so dequeue() returns and
    // the completion thread exits its loop.
    self->params->jpeg_encoder->stop_workers();

    if (self->params->completion_thread.joinable())
    {
        self->params->completion_thread.join();
    }
    return TRUE;
}

static gboolean gst_hailojpegenc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(encoder);

    GstCaps *output_caps = gst_caps_new_simple("image/jpeg", "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH(&state->info),
                                               "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT(&state->info), "framerate",
                                               GST_TYPE_FRACTION, GST_VIDEO_INFO_FPS_N(&state->info),
                                               GST_VIDEO_INFO_FPS_D(&state->info), "sof-marker", G_TYPE_INT, 0, NULL);

    GstVideoCodecState *out_state = gst_video_encoder_set_output_state(encoder, output_caps, state);
    if (out_state == nullptr)
    {
        GST_ERROR_OBJECT(self, "Failed to set output state");
        return FALSE;
    }
    gst_video_codec_state_unref(out_state);
    return gst_video_encoder_negotiate(encoder);
}

static GstFlowReturn gst_hailojpegenc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame)
{
    GstHailoJpegEnc *self = GST_HAILOJPEGENC(encoder);

    GstCapsPtr caps = gst_pad_get_current_caps(GST_VIDEO_ENCODER_SINK_PAD(encoder));
    HailoMediaLibraryBufferPtr hailo_buffer = hailo_buffer_from_gst_buffer(frame->input_buffer, caps);
    if (!hailo_buffer)
    {
        GST_ERROR_OBJECT(self, "Failed to wrap input GstBuffer for JpegEncoder");
        gst_video_codec_frame_unref(frame);
        return GST_FLOW_ERROR;
    }

    auto rc = self->params->jpeg_encoder->submit(hailo_buffer);
    if (rc != MEDIA_LIBRARY_SUCCESS)
    {
        GST_ERROR_OBJECT(self, "JpegEncoder::submit returned %d", static_cast<int>(rc));
        gst_video_codec_frame_unref(frame);
        return rc == MEDIA_LIBRARY_UNINITIALIZED ? GST_FLOW_FLUSHING : GST_FLOW_ERROR;
    }

    // stop_completion is checked under pending_mtx -- the lock stop() also takes -- so a frame
    // can't slip in after the completion thread has exited. Trade-off: if stop_completion
    // flips between submit() above and the lock here, the submitted job becomes a bounded
    // orphan in JpegEncoder's m_out_queue (no matching pending_frames entry, so the
    // completion thread won't dequeue it). It's reclaimed when the encoder is next
    // configure()d or destroyed -- bounded by n_threads and not a leak.
    {
        std::lock_guard<std::mutex> lock(self->params->pending_mtx);
        if (self->params->stop_completion.load())
        {
            gst_video_codec_frame_unref(frame);
            return GST_FLOW_FLUSHING;
        }
        self->params->pending_frames.push(frame);
    }
    self->params->pending_cv.notify_one();
    return GST_FLOW_OK;
}
