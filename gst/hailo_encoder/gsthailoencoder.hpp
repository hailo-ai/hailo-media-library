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
#pragma once

#include "media_library/encoder_class.hpp"
#include "media_library/media_library_utils.hpp"
#include "common/gstmedialibcommon.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <queue>

G_BEGIN_DECLS

typedef struct _GstHailoEncoder GstHailoEncoder;
typedef struct _GstHailoEncoderParams GstHailoEncoderParams;
typedef struct _GstHailoEncoderClass GstHailoEncoderClass;
#define GST_TYPE_HAILO_ENCODER (gst_hailo_encoder_get_type())
#define GST_HAILO_ENCODER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_ENCODER, GstHailoEncoder))
#define GST_HAILO_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_ENCODER, GstHailoEncoderClass))
#define GST_IS_HAILO_ENCODER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_ENCODER))
#define GST_IS_HAILO_ENCODER_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_ENCODER))
#define GST_HAILO_ENCODER_GET_CLASS(obj)                                                                               \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HAILO_ENCODER, GstHailoEncoderClass))

struct __attribute__((visibility("hidden"))) _GstHailoEncoderParams
{
    GstVideoCodecStatePtr input_state;
    std::unique_ptr<Encoder> encoder = nullptr;
    std::string config;
    std::string config_path;
    encoder_config_t encoder_config;
    encoder_config_t encoder_user_config;
    encoder_monitors encoder_monitors;
    gboolean stream_restart = false;
    std::queue<uint64_t> dts_queue;
    gboolean enforce_caps = true;
};

struct __attribute__((visibility("hidden"))) _GstHailoEncoder
{
    GstVideoEncoder parent;
    GstHailoEncoderParams *params = nullptr;
};

struct _GstHailoEncoderClass
{
    GstVideoEncoderClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_encoder_get_type(void);

G_END_DECLS
