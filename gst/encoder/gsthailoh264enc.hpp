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
/**
 * @file gsthailoh264enc.hpp
 * @brief Gstreamer Hailo H264 Encoder class module
 **/

#pragma once

#include "gsthailoenc.hpp"
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstHailoH264Enc GstHailoH264Enc;
typedef struct _GstHailoH264EncClass GstHailoH264EncClass;
#define GST_TYPE_HAILO_H264_ENC (gst_hailoh264enc_get_type())
#define GST_HAILO_H264_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_H264_ENC, GstHailoH264Enc))
#define GST_HAILO_H264_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_H264_ENC, GstHailoH264EncClass))
#define GST_IS_HAILO_H264_ENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_H264_ENC))
#define GST_IS_HAILO_H264_ENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_H264_ENC))
#define GST_HAILO_H264_ENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HAILO_H264_ENC, GstHailoH264EncClass))

struct _GstHailoH264Enc
{
  GstHailoEnc parent;
};

struct _GstHailoH264EncClass
{
  GstHailoEncClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailoh264enc_get_type(void);

G_END_DECLS
