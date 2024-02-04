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
                                  g_param_spec_string("config-str", "Json config", "Json config as string",
                                                      NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
  g_object_class_install_property(gobject_class, PROP_CONFIG_PATH,
                                  g_param_spec_string("config-path", "Json config path", "Json config as file path",
                                                      NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  venc_class->open = gst_hailo_encoder_open;
  venc_class->start = gst_hailo_encoder_start;
  venc_class->stop = gst_hailo_encoder_stop;
  venc_class->finish = gst_hailo_encoder_finish;
  venc_class->handle_frame = gst_hailo_encoder_handle_frame;
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
  hailoencoder->dts_queue = g_queue_new();
  hailoencoder->encoder = nullptr;
  g_queue_init(hailoencoder->dts_queue);
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
    g_value_set_string(value, hailoencoder->config.c_str());
    break;
  case PROP_CONFIG_PATH:
    g_value_set_string(value, hailoencoder->config_path.c_str());
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
gst_hailo_encoder_set_property(GObject *object,
                               guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstHailoEncoder *hailoencoder = (GstHailoEncoder *)(object);

  switch (prop_id)
  {
  case PROP_CONFIG_STRING:
    hailoencoder->config = std::string(g_value_get_string(value));
    break;
  case PROP_CONFIG_PATH:
    hailoencoder->config_path = std::string(g_value_get_string(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
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

static inline bool hailo_media_library_encoder_unref(PtrWrapper *wrapper)
{
  HailoMediaLibraryBufferPtr buffer = wrapper->ptr;
  delete wrapper;
  return buffer->decrease_ref_count();
}

static GstBuffer *
gst_hailo_encoder_get_output_buffer(GstHailoEncoder *hailoencoder,
                                    EncoderOutputBuffer output)
{
  PtrWrapper *wrapper = new PtrWrapper();
  wrapper->ptr = output.buffer;
  return gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                     output.buffer->get_plane(0),
                                     output.buffer->get_plane_size(0),
                                     0, output.size, wrapper, GDestroyNotify(hailo_media_library_encoder_unref));
}

/**
 * Gets the time difference between 2 time specs in milliseconds.
 * @param[in] after     The second time spec.
 * @param[in] before    The first time spec.\
 * @returns The time differnece in milliseconds.
 */
int64_t gst_hailo_encoder_difftimespec_ms(const struct timespec after, const struct timespec before)
{
  return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000 + ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec) / 1000000;
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
    GST_ERROR_OBJECT(hailoencoder, "Both config and config-path are provided");
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

  hailoencoder->encoder = std::make_unique<Encoder>(hailoencoder->config);
  return TRUE;
}

static gboolean
gst_hailo_encoder_start(GstVideoEncoder *encoder)
{
  GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;
  GST_DEBUG_OBJECT(hailoencoder, "hailoencoder start callback");
  hailoencoder->stream_restart = FALSE;
  auto output = hailoencoder->encoder->start();
  GstBuffer *buf = gst_hailo_encoder_get_output_buffer(hailoencoder, output);
  gst_buffer_add_hailo_buffer_meta(buf, output.buffer, output.size);
  gst_hailo_encoder_add_headers(hailoencoder, buf);

  gst_video_encoder_set_min_pts(encoder, 0);

  return TRUE;
}

static gboolean
gst_hailo_encoder_stop(GstVideoEncoder *encoder)
{
  GstHailoEncoder *hailoencoder = (GstHailoEncoder *)encoder;

  g_queue_free(hailoencoder->dts_queue);
  return TRUE;
}

static GstFlowReturn
gst_hailo_encoder_finish(GstVideoEncoder *encoder)
{
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
    input_frame = gst_video_encoder_get_oldest_frame(encoder);
  }
  else
  {
    input_frame = frame;
  }

  HailoMediaLibraryBufferPtr hailo_buffer_ptr = hailo_buffer_from_gst_buffer(frame->input_buffer, hailoencoder->input_state->caps);
  if (!hailo_buffer_ptr)
  {
    GST_ERROR_OBJECT(hailoencoder, "Could not get hailo buffer");
    return GST_FLOW_ERROR;
  }
  auto outputs = hailoencoder->encoder->handle_frame(hailo_buffer_ptr);
  gst_video_codec_frame_unref(input_frame);

  for (EncoderOutputBuffer output : outputs)
  {
    auto oldest_frame = gst_video_encoder_get_oldest_frame(encoder);
    oldest_frame->output_buffer = gst_hailo_encoder_get_output_buffer(hailoencoder, output);
    gst_buffer_add_hailo_buffer_meta(oldest_frame->output_buffer, output.buffer, output.size);
    gst_video_encoder_finish_frame(encoder, oldest_frame);
  }

  // if (is_keyframe && (frame == input_frame))
  // {
  //   gst_video_codec_frame_unref(input_frame);
  // }

  clock_gettime(CLOCK_MONOTONIC, &end_handle);
  GST_DEBUG_OBJECT(hailoencoder, "handle_frame took %lu milliseconds", (long)gst_hailo_encoder_difftimespec_ms(end_handle, start_handle));
  ret = GST_FLOW_OK;
  return ret;
}
