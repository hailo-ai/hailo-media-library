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
 * @file gsthailovisionpreproc.hpp
 * @brief  GStreamer HAILO_VISION_PREPROC element implementation
 *
 **/

#pragma once

#include <gst/gst.h>
#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include "media_library/vision_pre_proc.hpp"

G_BEGIN_DECLS


// Define HailoVisionPreProc type
#define GST_TYPE_HAILO_VISION_PREPROC \
  (gst_hailo_vision_preproc_get_type())
#define GST_HAILO_VISION_PREPROC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_VISION_PREPROC, GstHailoVisionPreProc))
#define GST_HAILO_VISION_PREPROC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_VISION_PREPROC, GstHailoVisionPreProcClass))
#define GST_IS_HAILO_VISION_PREPROC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_VISION_PREPROC))
#define GST_IS_HAILO_VISION_PREPROC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_VISION_PREPROC))
#define GST_HAILO_VISION_PREPROC_CAST(obj) ((GstHailoVisionPreProc *)(obj))

typedef struct _GstHailoVisionPreProc GstHailoVisionPreProc;
typedef struct _GstHailoVisionPreProcClass GstHailoVisionPreProcClass;

struct _GstHailoVisionPreProc {
  GstElement element;

  GstPad *sinkpad;
  std::vector<GstPad *> srcpads;
  gchar *config_file_path;
  std::string config_string;

  std::shared_ptr<MediaLibraryVisionPreProc> medialib_vision_pre_proc;
};

struct _GstHailoVisionPreProcClass {
  GstElementClass parent_class;

  /* Add class variables here */
};

G_GNUC_INTERNAL GType gst_hailo_vision_preproc_get_type(void);

G_END_DECLS