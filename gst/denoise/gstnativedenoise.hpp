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
 * @file gsthailodenoise.hpp
 * @brief  GStreamer HAILO_DENOISE element implementation
 *
 **/

#pragma once

#include "media_library/denoise.hpp"
#include <fstream>
#include <gst/gst.h>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

G_BEGIN_DECLS

// Define HailoDenoise type
#define GST_TYPE_HAILO_DENOISE (gst_hailo_denoise_get_type())
#define GST_HAILO_DENOISE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DENOISE, GstHailoDenoise))
#define GST_HAILO_DENOISE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DENOISE, GstHailoDenoiseClass))
#define GST_IS_HAILO_DENOISE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DENOISE))
#define GST_IS_HAILO_DENOISE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DENOISE))
#define GST_HAILO_DENOISE_CAST(obj) ((GstHailoDenoise *)(obj))

typedef struct _GstHailoDenoise GstHailoDenoise;
typedef struct _GstHailoDenoiseClass GstHailoDenoiseClass;

struct _GstHailoDenoise
{
    GstElement element;

    GstPad *sinkpad;
    GstPad *srcpad;
    gchar *config_file_path;
    gchar *config_string;

    gboolean m_flushing;
    std::unique_ptr<std::condition_variable> m_condvar;
    std::shared_ptr<std::mutex> m_mutex;
    std::shared_ptr<std::mutex> m_config_mutex;
    uint8_t m_queue_size;
    std::shared_ptr<std::queue<GstBuffer *>> m_staging_queue;

    std::shared_ptr<MediaLibraryDenoise> medialib_denoise;
    std::shared_ptr<denoise_config_t> denoise_config;
    media_library_return observe(const MediaLibraryDenoise::callbacks_t &callback)
    {
        return medialib_denoise->observe(callback);
    }
};

struct _GstHailoDenoiseClass
{
    GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_hailo_denoise_get_type(void);

G_END_DECLS
