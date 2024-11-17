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
/**
 * @file gsthailoencodebin.hpp
 * @brief  GStreamer HAILO_ENCODE_BIN bin element implementation
 *
 **/

#pragma once

#include <thread>
#include <fstream>
#include <memory>
#include <gst/gst.h>
#include <tl/expected.hpp> 
#include "media_library/media_library_types.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_ENCODE_BIN (gst_hailoencodebin_get_type())
#define GST_HAILO_ENCODE_BIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_ENCODE_BIN, GstHailoEncodeBin))
#define GST_HAILO_ENCODE_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_ENCODE_BIN, GstHailoEncodeBinClass))
#define GST_IS_HAILO_ENCODE_BIN(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_ENCODE_BIN))
#define GST_IS_HAILO_ENCODE_BIN_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_ENCODE_BIN))

typedef struct _GstHailoEncodeBin GstHailoEncodeBin;
typedef struct _GstHailoEncodeBinClass GstHailoEncodeBinClass;

struct _GstHailoEncodeBin
{
    GstBin base_hailoencodebin;
    GstPad *sinkpad;
    GstPad *srcpad;
    
    gchar *config_file_path;
    std::string config_string;
    EncoderType encoder_type;

    gboolean m_elements_linked;
    GstElement *m_osd;
    GstElement *m_queue_encoder;
    GstElement *m_encoder;
    guint queue_size;
};

struct _GstHailoEncodeBinClass
{
    GstBinClass base_hailoencodebin_class;
};

GType gst_hailoencodebin_get_type(void);

G_END_DECLS