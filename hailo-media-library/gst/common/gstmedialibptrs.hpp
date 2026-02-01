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
#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsrc.h>
#include <memory>
#include <functional>

template <typename GST_TYPE, typename UNREF_FUNC_TYPE = std::function<void(GST_TYPE *)>> class Unreffer
{
  private:
    UNREF_FUNC_TYPE unref_func;
    bool to_unref;

  public:
    void set_to_unref(bool should_unref)
    {
        to_unref = should_unref;
    }
    Unreffer(UNREF_FUNC_TYPE func) : unref_func(func), to_unref(true) {};
    Unreffer(UNREF_FUNC_TYPE func, bool _to_unref) : unref_func(func), to_unref(_to_unref) {};
    void operator()(GST_TYPE *ptr) const
    {
        if (to_unref && ptr != nullptr)
        {
            unref_func(ptr);
        }
    };
};

template <typename GST_TYPE, auto unref_func> class GstPtr : public std::unique_ptr<GST_TYPE, Unreffer<GST_TYPE>>
{
  public:
    GstPtr(GST_TYPE *ptr) : std::unique_ptr<GST_TYPE, Unreffer<GST_TYPE>>(ptr, Unreffer<GST_TYPE>(unref_func)) {};
    GstPtr() : std::unique_ptr<GST_TYPE, Unreffer<GST_TYPE>>(nullptr, Unreffer<GST_TYPE>(unref_func)) {};

    // Implicit conversion operator to use in GStreamer functions expecting GST_TYPE*
    operator GST_TYPE *() const
    {
        return this->get();
    }

    void set_auto_unref(bool should_unref)
    {
        this->get_deleter().set_to_unref(should_unref);
    }

    inline GObject *as_g_object()
    {
        return G_OBJECT(this->get());
    }
};

namespace glib_cpp::ptrs
{
void caps_unreffer(GstCaps *caps);
void buffer_unreffer(GstBuffer *buffer);
void sample_unreffer(GstSample *sample);
void element_unreffer(GstElement *element);
void pad_unreffer(GstPad *pad);
void allocator_unreffer(GstAllocator *allocator);
void pad_template_unreffer(GstPadTemplate *pad_template);
void bus_unreffer(GstBus *bus);
void message_unreffer(GstMessage *message);
void event_unreffer(GstEvent *event);
void video_codec_state_unreffer(GstVideoCodecState *video_codec_state);
void video_codec_frame_unreffer(GstVideoCodecFrame *video_codec_frame);
void tag_list_unreffer(GstTagList *tag_list);
void main_loop_unreffer(GMainLoop *main_loop);
void appsrc_unreffer(GstAppSrc *appsrc);
} // namespace glib_cpp::ptrs

using GstCapsPtr = GstPtr<GstCaps, &glib_cpp::ptrs::caps_unreffer>;
using GstBufferPtr = GstPtr<GstBuffer, &glib_cpp::ptrs::buffer_unreffer>;
using GstSamplePtr = GstPtr<GstSample, &glib_cpp::ptrs::sample_unreffer>;
using GstElementPtr = GstPtr<GstElement, &glib_cpp::ptrs::element_unreffer>;
using GstPadPtr = GstPtr<GstPad, &glib_cpp::ptrs::pad_unreffer>;
using GstAllocatorPtr = GstPtr<GstAllocator, &glib_cpp::ptrs::allocator_unreffer>;
using GstPadTemplatePtr = GstPtr<GstPadTemplate, &glib_cpp::ptrs::pad_template_unreffer>;
using GstBusPtr = GstPtr<GstBus, &glib_cpp::ptrs::bus_unreffer>;
using GstMessagePtr = GstPtr<GstMessage, &glib_cpp::ptrs::message_unreffer>;
using GstEventPtr = GstPtr<GstEvent, &glib_cpp::ptrs::event_unreffer>;
using GstVideoCodecStatePtr = GstPtr<GstVideoCodecState, &glib_cpp::ptrs::video_codec_state_unreffer>;
using GstVideoCodecFramePtr = GstPtr<GstVideoCodecFrame, &glib_cpp::ptrs::video_codec_frame_unreffer>;
using GstTagListPtr = GstPtr<GstTagList, &glib_cpp::ptrs::tag_list_unreffer>;
using GMainLoopPtr = GstPtr<GMainLoop, &glib_cpp::ptrs::main_loop_unreffer>;
using GstAppSrcPtr = GstPtr<GstAppSrc, &glib_cpp::ptrs::appsrc_unreffer>;
