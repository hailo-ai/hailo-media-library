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
#include <assert.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include "gsthailobuffermeta.hpp"
#include "gsthailoencoder.hpp"
#include "buffer_utils/buffer_utils.hpp"

/*******************
Property Definitions
*******************/
enum
{
    PROP_0,
    PROP_CONFIG_STRING,
    PROP_CONFIG_PATH,
    PROP_CONFIG,
    PROP_ENFORCE_CAPS,
    PROP_USER_CONFIG,
    NUM_OF_PROPS,
};

/************
Pad Templates
************/

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS("video/x-raw, "
                                                                                    "format=NV12, "
                                                                                    "width=(int)[16,MAX], "
                                                                                    "height=(int)[16,MAX], "
                                                                                    "framerate=(fraction)[0/1,MAX]"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS("video/x-h264, "
                                                                                   "stream-format = (string) byte-stream, "
                                                                                   "alignment = (string) au, "
                                                                                   "profile = (string) { base, main, high };"
                                                                                   "video/x-h265, "
                                                                                   "stream-format = (string) byte-stream, "
                                                                                   "alignment = (string) au, "
                                                                                   "profile = (string) { main, main-still-picture, main-intra, main-10, main-10-intra }"));

/*******************
Function Definitions
*******************/

static void gst_hailo_encoder_class_init(GstHailoEncoderClass *klass);
static void gst_hailo_encoder_init(GstHailoEncoder *hailoencoder);
static void gst_hailo_encoder_set_property(GObject *object,
                                           guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_hailo_encoder_get_property(GObject *object,
                                           guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_hailo_encoder_finalize(GObject *object);
static void gst_hailo_encoder_dispose(GObject *object);

static gboolean gst_hailo_encoder_open(GstVideoEncoder *encoder);
static gboolean gst_hailo_encoder_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static gboolean gst_hailo_encoder_propose_allocation(GstVideoEncoder *encoder, GstQuery *query);
static gboolean gst_hailo_encoder_flush(GstVideoEncoder *encoder);
static gboolean gst_hailo_encoder_start(GstVideoEncoder *encoder);
static gboolean gst_hailo_encoder_stop(GstVideoEncoder *encoder);
static GstFlowReturn gst_hailo_encoder_finish(GstVideoEncoder *encoder);
static GstFlowReturn gst_hailo_encoder_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);
static GstCaps *gst_hailo_encoder_getcaps(GstVideoEncoder *encoder, GstCaps *filter);

/*************
Init Functions
*************/

GST_DEBUG_CATEGORY_STATIC(gst_hailo_encoder_debug);
#define GST_CAT_DEFAULT gst_hailo_encoder_debug
#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_hailo_encoder_debug, "hailoencoder", 0, "hailoencoder element");
#define gst_hailo_encoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstHailoEncoder, gst_hailo_encoder, GST_TYPE_VIDEO_ENCODER, _do_init);

static void
gst_hailo_encoder_class_init(GstHailoEncoderClass *klass)
{
    GObjectClass *gobject_class;
    GstVideoEncoderClass *venc_class;
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class = (GObjectClass *)klass;
    venc_class = (GstVideoEncoderClass *)klass;

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_set_static_metadata(element_class,
                                          "H264/H265 Encoder",
                                          "Encoder/Video",
                                          "Encodes raw video into H264/H265 format",
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailo_encoder_set_property;
    gobject_class->get_property = gst_hailo_encoder_get_property;

    g_object_class_install_property(gobject_class, PROP_CONFIG_STRING,
                                    g_param_spec_string("config-string", "Json config", "Json config as string",
                                                        NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_CONFIG_PATH,
                                    g_param_spec_string("config-file-path", "Json config path", "Json config as file path",
                                                        NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_ENFORCE_CAPS,
                                    g_param_spec_boolean("enforce-caps", "Enforece caps",
                                                         "Enforce caps on the input/output pad of the bin",
                                                         TRUE,
                                                         (GParamFlags)(GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_pointer("config", "Encoder config", "Encoder config as encoder_config_t",
                                                         (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(gobject_class, PROP_USER_CONFIG,
                                    g_param_spec_pointer("user-config", "Encoder user config", "Encoder user config, used for setting the encoder configuration",
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

    venc_class->open = gst_hailo_encoder_open;
    venc_class->start = gst_hailo_encoder_start;
    venc_class->stop = gst_hailo_encoder_stop;
    venc_class->finish = gst_hailo_encoder_finish;
    venc_class->handle_frame = gst_hailo_encoder_handle_frame;
    venc_class->getcaps = gst_hailo_encoder_getcaps;
    venc_class->set_format = gst_hailo_encoder_set_format;
    venc_class->propose_allocation = gst_hailo_encoder_propose_allocation;
    venc_class->flush = gst_hailo_encoder_flush;

    gobject_class->finalize = gst_hailo_encoder_finalize;
    gobject_class->dispose = gst_hailo_encoder_dispose;
}

static void
gst_hailo_encoder_init(GstHailoEncoder *hailoencoder)
{
    hailoencoder->stream_restart = FALSE;
    hailoencoder->encoder = nullptr;
    hailoencoder->enforce_caps = TRUE;
}

/************************
GObject Virtual Functions
************************/

static void
gst_hailo_encoder_get_property(GObject *object,
                               guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)(object);

    switch (prop_id)
    {
    case PROP_CONFIG_STRING:
    {
        g_value_set_string(value, hailoencoder->config.c_str());
        break;
    }
    case PROP_CONFIG_PATH:
    {
        g_value_set_string(value, hailoencoder->config_path.c_str());
        break;
    }
    case PROP_CONFIG:
    {
        if (hailoencoder->encoder)
        {
            hailoencoder->encoder_config = std::make_shared<encoder_config_t>(hailoencoder->encoder->get_config());
            g_value_set_pointer(value, hailoencoder->encoder_config.get());
        }
        else
        {
            g_value_set_pointer(value, nullptr);
        }
        break;
    }
    case PROP_USER_CONFIG:
    {
        if (hailoencoder->encoder)
        {
            hailoencoder->encoder_user_config = std::make_shared<encoder_config_t>(hailoencoder->encoder->get_user_config());
            g_value_set_pointer(value, hailoencoder->encoder_user_config.get());
        }
        else
        {
            g_value_set_pointer(value, nullptr);
        }
        break;
    }
    case PROP_ENFORCE_CAPS:
    {
        g_value_set_boolean(value, hailoencoder->enforce_caps);
        break;
    }
    default:
    {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
    }
}

static void
gst_hailo_encoder_set_property(GObject *object,
                               guint prop_id, const GValue *value, GParamSpec *pspec)
{
    std::string encoder_config_path;
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)(object);

    switch (prop_id)
    {
    case PROP_CONFIG_STRING:
    {
        hailoencoder->config = std::string(g_value_get_string(value));
        break;
    }
    case PROP_CONFIG_PATH:
    {
        // Why do we need two lines instead of one? Good question!
        // For some odd reason, its not working when we use g_value_get_string directly
        encoder_config_path = std::string(g_value_get_string(value));
        hailoencoder->config_path = encoder_config_path;
        break;
    }
    case PROP_USER_CONFIG:
    {
        if (hailoencoder->encoder)
        {
            encoder_config_t *encoder_config = static_cast<encoder_config_t *>(g_value_get_pointer(value));
            if (hailoencoder->encoder->configure(*encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
            {
                GST_ERROR_OBJECT(hailoencoder, "Failed to configure encoder");
            }
            else
            {
                hailoencoder->encoder_config = std::make_shared<encoder_config_t>(*encoder_config);
            }
        }
        else
        {
            GST_ERROR_OBJECT(hailoencoder, "Encoder instance not initialized");
        }
        break;
    }
    case PROP_ENFORCE_CAPS:
    {
        hailoencoder->enforce_caps = g_value_get_boolean(value);
        break;
    }
    default:
    {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
    }
}

static void
gst_hailo_encoder_finalize(GObject *object)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)object;
    GST_DEBUG_OBJECT(hailoencoder, "hailoencoder finalize callback");

    /* clean up remaining allocated data */

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_hailo_encoder_dispose(GObject *object)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)object;
    GST_DEBUG_OBJECT(hailoencoder, "hailoencoder dispose callback");

    /* clean up as possible.  may be called multiple times */
    if (hailoencoder->encoder)
    {
        hailoencoder->encoder->dispose();
        hailoencoder->encoder.reset();
        hailoencoder->encoder = nullptr;
    }
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*****************
Internal Functions
*****************/

struct PtrWrapper
{
    HailoMediaLibraryBufferPtr ptr;
};

static inline void hailo_media_library_encoder_release(PtrWrapper *wrapper)
{
    delete wrapper;
}

static GstBuffer *
gst_hailo_encoder_get_output_buffer(GstHailoEncoder *hailoencoder,
                                    EncoderOutputBuffer output)
{
    PtrWrapper *wrapper = new PtrWrapper();
    wrapper->ptr = output.buffer;
    return gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                       output.buffer->get_plane_ptr(0),
                                       output.buffer->get_plane_size(0),
                                       0, output.size, wrapper, GDestroyNotify(hailo_media_library_encoder_release));
}

/**
 * Add headers to the encoded stream
 *
 * @param[in] hailoencoder       The hailoencoder gstreamer instance.
 * @param[in] new_header     The new header as GstBuf fer object.
 */
static void
gst_hailo_encoder_add_headers(GstHailoEncoder *hailoencoder, GstBuffer *new_header)
{
    GList *l = g_list_append(NULL, new_header);
    gst_video_encoder_set_headers(GST_VIDEO_ENCODER(hailoencoder), l);
}

static GstCaps *
gst_hailo_encoder_getcaps(GstVideoEncoder *encoder, GstCaps *filter)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    GstCaps *caps;
    if (!hailoencoder->encoder)
    {
        GST_DEBUG_OBJECT(hailoencoder, "Encoder instance not initialized - Getting proxy caps");
        caps = gst_video_encoder_proxy_getcaps(encoder, NULL, filter);
    }
    else if (!hailoencoder->enforce_caps)
    {
        GST_DEBUG_OBJECT(hailoencoder, "Enforce caps is disabled - Getting proxy caps");
        caps = gst_video_encoder_proxy_getcaps(encoder, NULL, filter);
    }
    else
    {
        GST_DEBUG_OBJECT(hailoencoder, "Getting caps from encoder instance");
        input_config_t input_config = std::get<hailo_encoder_config_t>(hailoencoder->encoder->get_config()).input_stream;
        caps = gst_caps_new_empty_simple("video/x-raw");
        gst_caps_set_simple(caps,
                            "format", G_TYPE_STRING, input_config.format.c_str(),
                            "width", G_TYPE_INT, input_config.width,
                            "height", G_TYPE_INT, input_config.height,
                            "framerate", GST_TYPE_FRACTION, input_config.framerate, 1,
                            NULL);
        gst_caps_set_features(caps, 0, gst_caps_features_new_any());
    }
    return caps;
}

static gboolean
gst_hailo_encoder_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
    // GstCaps *other_caps;
    GstCaps *allowed_caps;
    GstCaps *icaps;
    GstVideoCodecState *output_format;
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;

    /* some codecs support more than one format, first auto-choose one */
    GST_DEBUG_OBJECT(hailoencoder, "picking an output format ...");
    allowed_caps = gst_pad_get_allowed_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder));
    if (!allowed_caps)
    {
        GST_DEBUG_OBJECT(hailoencoder, "... but no peer, using template caps");
        /* we need to copy because get_allowed_caps returns a ref, and
         * get_pad_template_caps doesn't */
        allowed_caps =
            gst_pad_get_pad_template_caps(GST_VIDEO_ENCODER_SRC_PAD(encoder));
    }
    GST_DEBUG_OBJECT(hailoencoder, "chose caps %" GST_PTR_FORMAT, allowed_caps);

    icaps = gst_caps_fixate(allowed_caps);

    /* Store input state and set output state */
    if (hailoencoder->input_state)
        gst_video_codec_state_unref(hailoencoder->input_state);
    hailoencoder->input_state = gst_video_codec_state_ref(state);
    GST_DEBUG_OBJECT(hailoencoder, "Setting output caps state %" GST_PTR_FORMAT, icaps);

    output_format = gst_video_encoder_set_output_state(encoder, icaps, state);

    gst_video_codec_state_unref(output_format);

    /* Store some tags */
    {
        GstTagList *tags = gst_tag_list_new_empty();
        gst_video_encoder_merge_tags(encoder, tags, GST_TAG_MERGE_REPLACE);
        gst_tag_list_unref(tags);
    }
    gint max_delayed_frames = 5;
    GstClockTime latency;
    latency = gst_util_uint64_scale_ceil(GST_SECOND * 1, max_delayed_frames, 25);
    gst_video_encoder_set_latency(GST_VIDEO_ENCODER(encoder), latency, latency);

    return TRUE;
}

static gboolean
gst_hailo_encoder_propose_allocation(GstVideoEncoder *encoder, GstQuery *query)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    GST_DEBUG_OBJECT(hailoencoder, "hailoencoder propose allocation callback");

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

    return GST_VIDEO_ENCODER_CLASS(parent_class)->propose_allocation(encoder, query);
}

static gboolean
gst_hailo_encoder_flush(GstVideoEncoder *encoder)
{
    return TRUE;
}

static gboolean
gst_hailo_encoder_open(GstVideoEncoder *encoder)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    if (!hailoencoder->config_path.empty() &&
        !hailoencoder->config.empty())
    {
        GST_ERROR_OBJECT(hailoencoder, "Both config and config-file-path are provided");
        return FALSE;
    }
    else if (hailoencoder->config_path.empty() &&
             hailoencoder->config.empty())
    {
        GST_ERROR_OBJECT(hailoencoder, "No config provided");
        return FALSE;
    }

    if (!hailoencoder->config_path.empty())
    {
        GST_DEBUG_OBJECT(hailoencoder, "Using config file");
        std::ifstream input_json(hailoencoder->config_path.c_str());
        std::stringstream json_buffer;
        json_buffer << input_json.rdbuf();
        hailoencoder->config = json_buffer.str();
    }
    else
    {
        GST_DEBUG_OBJECT(hailoencoder, "Using config string");
    }

    // Load overlays from json string
    std::string clean_config = hailoencoder->config;
    // in case there are quotes around the string, remove them - they were added to enable spaces in the string
    if (clean_config[0] == '\'' && clean_config[clean_config.size() - 1] == '\'')
    {
        clean_config = clean_config.substr(1, hailoencoder->config.size() - 2);
    }

    hailoencoder->config = clean_config;

    if (hailoencoder->encoder)
    {
        GST_DEBUG_OBJECT(hailoencoder, "Reusing encoder instance");
        hailoencoder->encoder->init();
    }
    else
    {
        GST_DEBUG_OBJECT(hailoencoder, "Creating new encoder instance");
        hailoencoder->encoder = std::make_unique<Encoder>(hailoencoder->config);
    }
    return TRUE;
}

static gboolean
gst_hailo_encoder_start(GstVideoEncoder *encoder)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    GST_DEBUG_OBJECT(hailoencoder, "hailoencoder start callback");
    hailoencoder->stream_restart = FALSE;
    hailoencoder->dts_queue = g_queue_new();
    g_queue_init(hailoencoder->dts_queue);

    auto output = hailoencoder->encoder->start();
    GstBuffer *buf = gst_hailo_encoder_get_output_buffer(hailoencoder, output);
    gst_buffer_add_hailo_buffer_meta(buf, output.buffer, output.size);
    gst_hailo_encoder_add_headers(hailoencoder, buf);

    gst_video_encoder_set_min_pts(encoder, GST_SECOND);

    return TRUE;
}

static gboolean
gst_hailo_encoder_stop(GstVideoEncoder *encoder)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    GST_DEBUG_OBJECT(hailoencoder, "hailoencoder stop callback");
    hailoencoder->encoder->release();
    g_queue_free(hailoencoder->dts_queue);
    return TRUE;
}

static GstFlowReturn
gst_hailo_encoder_finish(GstVideoEncoder *encoder)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;

    GST_DEBUG_OBJECT(hailoencoder, "hailoencoder finish callback");
    auto output = hailoencoder->encoder->stop();
    GstBuffer *eos_buffer = gst_hailo_encoder_get_output_buffer(hailoencoder, output);
    gst_buffer_add_hailo_buffer_meta(eos_buffer, output.buffer, output.size);

    eos_buffer->pts = eos_buffer->dts = GPOINTER_TO_UINT(g_queue_peek_tail(hailoencoder->dts_queue));

    return gst_pad_push(GST_VIDEO_ENCODER_SRC_PAD(encoder), eos_buffer);
}

static GstFlowReturn
gst_hailo_encoder_encode_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *input_frame)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    GstBuffer *null_buffer;
    HailoMediaLibraryBufferPtr hailo_buffer_ptr = hailo_buffer_from_gst_buffer(input_frame->input_buffer, hailoencoder->input_state->caps);
    if (!hailo_buffer_ptr)
    {
        GST_ERROR_OBJECT(hailoencoder, "Could not get hailo buffer");
        return GST_FLOW_ERROR;
    }

    GST_DEBUG_OBJECT(encoder, "Encode frame - calling handle_frame");
    auto outputs = hailoencoder->encoder->handle_frame(hailo_buffer_ptr, input_frame->system_frame_number);

    for (EncoderOutputBuffer &output : outputs)
    {
        auto current_frame = gst_video_encoder_get_frame(encoder, output.frame_number);
        if(current_frame == nullptr)
        {
            GST_ERROR_OBJECT(hailoencoder, "Failed to get oldest frame");
            return GST_FLOW_ERROR;
        }

        if (output.size == 0)
        {
            GST_INFO_OBJECT(hailoencoder, "Send null buffer");
            null_buffer = gst_buffer_new();
            gst_buffer_set_size(null_buffer, 0);
            current_frame->output_buffer = null_buffer;
        }
        else
        {
            current_frame->output_buffer = gst_hailo_encoder_get_output_buffer(hailoencoder, output);
        }

        current_frame->dts = GPOINTER_TO_UINT(g_queue_pop_head(hailoencoder->dts_queue));
        gst_buffer_add_hailo_buffer_meta(current_frame->output_buffer, output.buffer, output.size);
        if (gst_video_encoder_finish_frame(encoder, current_frame) != GST_FLOW_OK)
        {
            GST_WARNING_OBJECT(hailoencoder, "Failed to finish frame");
            return GST_FLOW_OK;
        }
    }

    return GST_FLOW_OK;
}

static GstFlowReturn
gst_hailo_encoder_handle_frame(GstVideoEncoder *encoder,
                               GstVideoCodecFrame *frame)
{
    GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
    GstFlowReturn ret = GST_FLOW_ERROR;
    GstVideoCodecFrame *input_frame = nullptr;
    struct timespec start_handle, end_handle;
    clock_gettime(CLOCK_MONOTONIC, &start_handle);
    GST_DEBUG_OBJECT(hailoencoder, "Received frame number %u", frame->system_frame_number);

    if (frame->system_frame_number == 0)
    {
        switch (hailoencoder->encoder->get_gop_size())
        {
        case 1:
            break;
        case 2:
        case 3:
            g_queue_push_tail(hailoencoder->dts_queue, GUINT_TO_POINTER(frame->pts - frame->duration));
            break;
        default:
            g_queue_push_tail(hailoencoder->dts_queue, GUINT_TO_POINTER(frame->pts - 2 * frame->duration));
            g_queue_push_tail(hailoencoder->dts_queue, GUINT_TO_POINTER(frame->pts - frame->duration));
            break;
        }
    }

    g_queue_push_tail(hailoencoder->dts_queue, GUINT_TO_POINTER(frame->pts));
    gboolean is_keyframe = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(frame);

    if (is_keyframe)
    {
        GST_DEBUG_OBJECT(hailoencoder, "Forcing keyframe");
        // Adding sync point in order to delete forced keyframe evnet from the queue.
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
        hailoencoder->encoder->force_keyframe();
        // Encode oldest frame of gop
        input_frame = gst_video_encoder_get_oldest_frame(encoder);
        ret = gst_hailo_encoder_encode_frame(encoder, input_frame);
        if (ret != GST_FLOW_OK)
            return ret;
    }

    if(input_frame != frame)
    {
        // Encode the current frame (if it is not the same as the oldest frame)
        ret = gst_hailo_encoder_encode_frame(encoder, frame);
        if (ret != GST_FLOW_OK)
            return ret;
    }

    // Unref both frames if needed
    if(input_frame != nullptr)
    {
        gst_video_codec_frame_unref(input_frame);
        input_frame = nullptr;
    }

    if(frame != nullptr)
    {
        gst_video_codec_frame_unref(frame);
        frame = nullptr;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_handle);
    GST_DEBUG_OBJECT(hailoencoder, "handle_frame took %lu milliseconds", (long)media_library_difftimespec_ms(end_handle, start_handle));
    return ret;
}
