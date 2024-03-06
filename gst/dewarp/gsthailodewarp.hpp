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
 * @file gsthailodewarp.hpp
 * @brief  GStreamer HAILO_DEWARP element implementation
 *
 **/

#pragma once

#include "media_library/dewarp.hpp"
#include <fstream>
#include <gst/gst.h>
#include <memory>
#include <string>
#include <vector>

G_BEGIN_DECLS

// Define HailoDewarp type
#define GST_TYPE_HAILO_DEWARP \
  (gst_hailo_dewarp_get_type())
#define GST_HAILO_DEWARP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DEWARP, GstHailoDewarp))
#define GST_HAILO_DEWARP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DEWARP, GstHailoDewarpClass))
#define GST_IS_HAILO_DEWARP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DEWARP))
#define GST_IS_HAILO_DEWARP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DEWARP))
#define GST_HAILO_DEWARP_CAST(obj) ((GstHailoDewarp *)(obj))

typedef struct _GstHailoDewarp GstHailoDewarp;
typedef struct _GstHailoDewarpClass GstHailoDewarpClass;

struct _GstHailoDewarp
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;
  gchar *config_file_path;
  std::string config_string;

  std::shared_ptr<MediaLibraryDewarp> medialib_dewarp;
};

struct _GstHailoDewarpClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_dewarp_get_type(void);

G_END_DECLS