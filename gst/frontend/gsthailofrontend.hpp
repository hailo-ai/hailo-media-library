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
 * @file gsthailofrontend.hpp
 * @brief  GStreamer HAILO_FRONTEND bin element implementation
 *
 **/

#pragma once

#include <thread>
#include <fstream>
#include <memory>
#include <functional>
#include <gst/gst.h>
#include <tl/expected.hpp> 
#include "media_library/denoise.hpp"
#include "media_library/media_library_types.hpp"
#include "denoise/gsthailodenoise.hpp"

G_BEGIN_DECLS

#define GST_TYPE_HAILO_FRONTEND (gst_hailofrontend_get_type())
#define GST_HAILO_FRONTEND(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_FRONTEND, GstHailoFrontend))
#define GST_HAILO_FRONTEND_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_FRONTEND, GstHailoFrontendClass))
#define GST_IS_HAILO_FRONTEND(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_FRONTEND))
#define GST_IS_HAILO_FRONTEND_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_FRONTEND))

typedef struct _GstHailoFrontend GstHailoFrontend;
typedef struct _GstHailoFrontendClass GstHailoFrontendClass;

struct _GstHailoFrontend
{
    GstBin base_hailofrontend;
    GstPad *sinkpad;
    std::vector<GstPad *> srcpads;
    
    gchar *config_file_path;
    std::string config_string;

    gboolean m_elements_linked;
    GstElement *m_dis_dewarp;
    GstElement *m_dewarp_mresize_queue;
    GstElement *m_multi_resize;

    media_library_return observe_denoising(const MediaLibraryDenoise::callbacks_t &callback) {
        // GstHailoDenoise* denoise = GST_HAILO_DENOISE(m_denoise);
        // return denoise->observe(callback);
        return MEDIA_LIBRARY_SUCCESS;
    }
};

struct _GstHailoFrontendClass
{
    GstBinClass base_hailofrontend_class;
};

GType gst_hailofrontend_get_type(void);

G_END_DECLS