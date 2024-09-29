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
 * @file gsthailomultiresize.hpp
 * @brief  GStreamer HAILO_MULTI_RESIZE element implementation
 *
 **/

#pragma once

#include "media_library/multi_resize.hpp"
#include <fstream>
#include <gst/gst.h>
#include <memory>
#include <string>
#include <vector>

G_BEGIN_DECLS

// Define HailoMultiResize type
#define GST_TYPE_HAILO_MULTI_RESIZE \
  (gst_hailo_multi_resize_get_type())
#define GST_HAILO_MULTI_RESIZE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_MULTI_RESIZE, GstHailoMultiResize))
#define GST_HAILO_MULTI_RESIZE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_MULTI_RESIZE, GstHailoMultiResizeClass))
#define GST_IS_HAILO_MULTI_RESIZE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_MULTI_RESIZE))
#define GST_IS_HAILO_MULTI_RESIZE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_MULTI_RESIZE))
#define GST_HAILO_MULTI_RESIZE_CAST(obj) ((GstHailoMultiResize *)(obj))

typedef struct _GstHailoMultiResize GstHailoMultiResize;
typedef struct _GstHailoMultiResizeClass GstHailoMultiResizeClass;

struct _GstHailoMultiResize
{
  GstElement element;

  GstPad *sinkpad;
  std::shared_ptr<std::vector<GstPad *>> srcpads;
  gchar *config_file_path;
  gchar *config_string;

  std::shared_ptr<multi_resize_config_t> multi_resize_config;
  std::shared_ptr<MediaLibraryMultiResize> medialib_multi_resize;
};

struct _GstHailoMultiResizeClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_multi_resize_get_type(void);

G_END_DECLS