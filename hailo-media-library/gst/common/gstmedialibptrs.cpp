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

#include "gstmedialibptrs.hpp"

namespace glib_cpp::ptrs
{
void caps_unreffer(GstCaps *caps)
{
    gst_caps_unref(caps);
}

void buffer_unreffer(GstBuffer *buffer)
{
    gst_buffer_unref(buffer);
}

void sample_unreffer(GstSample *sample)
{
    gst_sample_unref(sample);
}

void element_unreffer(GstElement *element)
{
    gst_object_unref(element);
}

void pad_unreffer(GstPad *pad)
{
    gst_object_unref(pad);
}

void allocator_unreffer(GstAllocator *allocator)
{
    gst_object_unref(allocator);
}

void pad_template_unreffer(GstPadTemplate *pad_template)
{
    gst_object_unref(pad_template);
}

void bus_unreffer(GstBus *bus)
{
    gst_object_unref(bus);
}

void message_unreffer(GstMessage *message)
{
    gst_message_unref(message);
}

void event_unreffer(GstEvent *event)
{
    gst_event_unref(event);
}

void video_codec_state_unreffer(GstVideoCodecState *video_codec_state)
{
    gst_video_codec_state_unref(video_codec_state);
}

void video_codec_frame_unreffer(GstVideoCodecFrame *video_codec_frame)
{
    gst_video_codec_frame_unref(video_codec_frame);
}

void tag_list_unreffer(GstTagList *tag_list)
{
    gst_tag_list_unref(tag_list);
}

void main_loop_unreffer(GMainLoop *main_loop)
{
    g_main_loop_unref(main_loop);
}

void appsrc_unreffer(GstAppSrc *appsrc)
{
    gst_object_unref(appsrc);
}

} // namespace glib_cpp::ptrs
