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
 * @file gsthailodefog.hpp
 * @brief  GStreamer HAILO_DEFOG element implementation
 *
 **/

#pragma once

#include <thread>
#include <map>
#include <queue>
#include <fstream>
#include <memory>
#include <gst/gst.h>
#include <tl/expected.hpp>
#include "media_library/defog.hpp"
#include "media_library/media_library_types.hpp"
#include "hailo/hailort.h"
#include "metadata/tensor_meta.hpp"


G_BEGIN_DECLS

#define GST_TYPE_HAILO_DEFOG (gst_hailodefog_get_type())
#define GST_HAILO_DEFOG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DEFOG, GstHailoDefog))
#define GST_HAILO_DEFOG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DEFOG, GstHailoDefogClass))
#define GST_IS_HAILO_DEFOG(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DEFOG))
#define GST_IS_HAILO_DEFOG_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DEFOG))

typedef struct _GstHailoDefog GstHailoDefog;
typedef struct _GstHailoDefogClass GstHailoDefogClass;

struct _GstHailoDefog
{
    GstBin base_hailodefog;
    GstPad *sinkpad;
    GstPad *srcpad;
    
    gchar *config_file_path;
    std::string config_string;
    std::shared_ptr<MediaLibraryDefog> medialib_defog;

    gboolean m_rotated;
    gboolean m_configured;
    gboolean m_elements_linked;
    GstElement *m_hailonet;
};

struct _GstHailoDefogClass
{
    GstBinClass base_hailodefog_class;
};

GType gst_hailodefog_get_type(void);

G_END_DECLS