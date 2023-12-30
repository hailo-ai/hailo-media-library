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
 * @file buffer_utils.hpp
 * @brief  GStreamer buffer to DSP buffer converstion utilities
 *
 **/

#pragma once

#include "media_library/buffer_pool.hpp"
#include "media_library/dsp_utils.hpp"
#include "hailo_v4l2/hailo_vsm.h"
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include <gst/video/video.h>
#include <stdint.h>
G_BEGIN_DECLS

GstBuffer *create_gst_buffer_from_hailo_buffer(HailoMediaLibraryBufferPtr hailo_buffer);
GstVideoMeta *add_video_meta_to_buffer(GstBuffer *buffer, GstVideoInfo *video_info, HailoMediaLibraryBufferPtr hailo_buffer);
bool create_hailo_buffer_from_video_frame(GstVideoFrame *video_frame, hailo_media_library_buffer &hailo_buffer, GstHailoV4l2Meta *meta = nullptr);
bool create_dsp_buffer_from_video_frame(GstVideoFrame *video_frame, dsp_image_properties_t &dsp_image_props);
bool create_dsp_buffer_from_video_info(GstBuffer *buffer, GstVideoInfo *video_info, dsp_image_properties_t &dsp_image_props);
void update_video_info_from_meta(GstBuffer *buffer, GstVideoInfo *video_info);

G_END_DECLS