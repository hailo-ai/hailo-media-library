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

#include <thread>
#include <map>
#include <mutex>
#include <queue>
#include <fstream>
#include <memory>
#include <functional>
#include <gst/gst.h>
#include <tl/expected.hpp> 
#include <condition_variable>
#include "media_library/denoise.hpp"
#include "media_library/media_library_types.hpp"
#include "hailo/hailort.h"
#include "metadata/tensor_meta.hpp"


G_BEGIN_DECLS

#define GST_TYPE_HAILO_DENOISE (gst_hailodenoise_get_type())
#define GST_HAILO_DENOISE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HAILO_DENOISE, GstHailoDenoise))
#define GST_HAILO_DENOISE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HAILO_DENOISE, GstHailoDenoiseClass))
#define GST_IS_HAILO_DENOISE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HAILO_DENOISE))
#define GST_IS_HAILO_DENOISE_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HAILO_DENOISE))

typedef struct _GstHailoDenoise GstHailoDenoise;
typedef struct _GstHailoDenoiseClass GstHailoDenoiseClass;

struct _GstHailoDenoise
{
    GstBin base_hailodenoise;
    GstPad *sinkpad;
    GstPad *srcpad;
    
    gchar *config_file_path;
    std::string config_string;
    std::shared_ptr<MediaLibraryDenoise> medialib_denoise;

    gboolean m_configured;
    gboolean m_elements_linked;
    GstElement *m_hailonet;

    std::unique_ptr<std::condition_variable> m_condvar;
    std::shared_ptr<std::mutex> m_mutex;
    uint8_t m_queue_size;
    uint8_t m_loop_counter;
    std::queue<GstBuffer *> m_loopback_queue;

    media_library_return observe(const MediaLibraryDenoise::callbacks_t &callback) {
        return medialib_denoise->observe(callback);
    }
};

struct _GstHailoDenoiseClass
{
    GstBinClass base_hailodenoise_class;
};

GType gst_hailodenoise_get_type(void);

G_END_DECLS