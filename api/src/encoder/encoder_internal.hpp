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
#pragma once
#include "media_library/encoder.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#define MIN_QUEUE_SIZE 5

struct InputParams
{
    std::string format;
    uint32_t width;
    uint32_t height;
    uint32_t framerate;
};
enum appsrc_state
{
    APPSRC_STATE_UNINITIALIZED = 0,
    APPSRC_STATE_NEED_DATA,
    APPSRC_STATE_ENOUGH_DATA
};
class MediaLibraryEncoder::Impl final
{
private:
    InputParams m_input_params;
    std::shared_ptr<std::mutex> m_mutex;
    std::unique_ptr<std::condition_variable> m_condvar;
    uint8_t m_queue_size;
    std::vector<AppWrapperCallback> m_callbacks;
    std::queue<GstBuffer *> m_queue;
    GstAppSrc *m_appsrc;
    GstCaps *m_appsrc_caps;
    GMainLoop *m_main_loop;
    std::shared_ptr<std::thread> m_main_loop_thread;
    GstElement *m_pipeline;
    guint m_send_buffer_id;
    std::string m_json_config; // TODO: this should be const
    std::shared_ptr<osd::Blender> m_blender;
    appsrc_state m_appsrc_state;

public:
    static tl::expected<std::shared_ptr<MediaLibraryEncoder::Impl>, media_library_return> create(std::string json_config);

    ~Impl();
    Impl(std::string json_config, media_library_return &status);

public:
    media_library_return subscribe(AppWrapperCallback callback);
    media_library_return start();
    media_library_return stop();
    media_library_return add_buffer(HailoMediaLibraryBufferPtr ptr);
    void add_gst_buffer(GstBuffer *buffer);

    std::shared_ptr<osd::Blender> get_blender();

    /**
     * Below are public functions that are not part of the public API
     * but are public for gstreamer callbacks.
     */
public:
    void on_need_data(GstAppSrc *appsrc, guint size);
    void on_enough_data(GstAppSrc *appsrc);
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate,
                            gdouble avgfps);
    GstFlowReturn on_new_sample(GstAppSink *appsink);
    gboolean on_idle_callback();
    gboolean on_bus_call(GstBus *bus, GstMessage *msg);

private:
    static void need_data(GstAppSrc *appsrc, guint size, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder =
            static_cast<MediaLibraryEncoder::Impl *>(user_data);
        encoder->on_need_data(appsrc, size);
    }
    static void enough_data(GstAppSrc *appsrc, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder =
            static_cast<MediaLibraryEncoder::Impl *>(user_data);
        encoder->on_enough_data(appsrc);
    }
    static void fps_measurement(GstElement *fpssink, gdouble fps,
                                gdouble droprate, gdouble avgfps,
                                gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder =
            static_cast<MediaLibraryEncoder::Impl *>(user_data);
        encoder->on_fps_measurement(fpssink, fps, droprate, avgfps);
    }
    static GstFlowReturn new_sample(GstAppSink *appsink, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder =
            static_cast<MediaLibraryEncoder::Impl *>(user_data);
        return encoder->on_new_sample(appsink);
    }
    static gboolean idle_callback(gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder =
            static_cast<MediaLibraryEncoder::Impl *>(user_data);
        return encoder->send_buffer();
    }
    static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
    {
        MediaLibraryEncoder::Impl *encoder =
            static_cast<MediaLibraryEncoder::Impl *>(user_data);
        return encoder->on_bus_call(bus, msg);
    }

private:
    void set_gst_callbacks(GstElement *pipeline);
    void add_buffer_internal(GstBuffer *buffer);
    gboolean send_buffer();
    gboolean push_buffer(GstBuffer *buffer);
    GstBuffer *dequeue_buffer();
    std::string create_pipeline_string(nlohmann::json osd_json_config);
};