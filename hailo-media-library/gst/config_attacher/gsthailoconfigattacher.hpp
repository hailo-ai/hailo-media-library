/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
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
 * @file gsthailoconfigattacher.hpp
 * @brief  GStreamer HAILO_CONFIG_ATTACHER element implementation
 *
 **/

#pragma once

#include <gst/gst.h>
#include <glib-object.h>
#include <glib.h>
#include <memory>

#include "common/gstmedialibptrs.hpp"
#include "media_library/config_attacher.hpp"
#include "media_library/config_manager.hpp"

G_BEGIN_DECLS

// Define HailoConfigAttacher type
#define GST_TYPE_HAILO_CONFIG_ATTACHER (gst_hailo_config_attacher_get_type())
#define GST_HAILO_CONFIG_ATTACHER(obj)                                                                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_CONFIG_ATTACHER, GstHailoConfigAttacher))
#define GST_HAILO_CONFIG_ATTACHER_CLASS(klass)                                                                         \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_CONFIG_ATTACHER, GstHailoConfigAttacherClass))
#define GST_IS_HAILO_CONFIG_ATTACHER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_CONFIG_ATTACHER))
#define GST_IS_HAILO_CONFIG_ATTACHER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_CONFIG_ATTACHER))
#define GST_HAILO_CONFIG_ATTACHER_CAST(obj) ((GstHailoConfigAttacher *)(obj))

typedef struct _GstHailoConfigAttacher GstHailoConfigAttacher;
typedef struct _GstHailoConfigAttacherParams GstHailoConfigAttacherParams;
typedef struct _GstHailoConfigAttacherClass GstHailoConfigAttacherClass;

struct __attribute__((visibility("hidden"))) _GstHailoConfigAttacherParams
{
    GstPadPtr sinkpad;
    GstPadPtr srcpad;

    ConfigManagerInteractor *config_manager_interactor = nullptr;
    bool m_is_config_manager_owner = false;
    std::unique_ptr<ConfigAttacher> config_attacher;
};

struct __attribute__((visibility("hidden"))) _GstHailoConfigAttacher
{
    GstElement element;
    GstHailoConfigAttacherParams *params = nullptr;
};

struct _GstHailoConfigAttacherClass
{
    GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_config_attacher_get_type(void);

G_END_DECLS
