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
#include "buffer_forwarder.hpp"

#include <utility>

GST_DEBUG_CATEGORY_STATIC(buffer_forwarder_debug);
#define GST_CAT_DEFAULT buffer_forwarder_debug

static void ensure_debug_category()
{
    static std::once_flag flag;
    std::call_once(
        flag, []() { GST_DEBUG_CATEGORY_INIT(buffer_forwarder_debug, "bufferforwarder", 0, "BufferForwarder debug"); });
}

BufferForwarder::BufferForwarder(GstPad *src_pad, std::string stream_id, GstElement *owner_element,
                                 size_t max_queue_size)
    : m_src_pad(src_pad), m_stream_id(std::move(stream_id)), m_owner_element(owner_element),
      m_max_queue_size(max_queue_size)
{
    ensure_debug_category();
}

BufferForwarder::~BufferForwarder()
{
    stop();
}

void BufferForwarder::enqueue(GstBuffer *buffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running)
    {
        gst_buffer_unref(buffer);
        return;
    }
    if (m_buffer_queue.size() >= m_max_queue_size)
    {
        GstBuffer *oldest = m_buffer_queue.front();
        m_buffer_queue.pop_front();
        gst_buffer_unref(oldest);
        m_needs_discontinuity = true;
        GST_DEBUG_OBJECT(m_owner_element, "BufferForwarder[%s]: queue full, dropped oldest buffer",
                         m_stream_id.c_str());
    }
    m_buffer_queue.push_back(buffer);
    m_buffer_available_cv.notify_one();
}

void BufferForwarder::start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = true;
    }
    m_worker_thread = std::thread(&BufferForwarder::worker_loop, this);
}

void BufferForwarder::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
            return;
        m_running = false;
    }
    m_buffer_available_cv.notify_all();
    if (m_worker_thread.joinable())
        m_worker_thread.join();
    drain();
}

void BufferForwarder::drain()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_buffer_queue.empty())
    {
        gst_buffer_unref(m_buffer_queue.front());
        m_buffer_queue.pop_front();
    }
}

void BufferForwarder::worker_loop()
{
    GST_DEBUG_OBJECT(m_owner_element, "BufferForwarder[%s]: worker started", m_stream_id.c_str());
    while (true)
    {
        GstBuffer *buffer = nullptr;
        bool has_discontinuity = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_buffer_available_cv.wait(lock, [this]() { return !m_buffer_queue.empty() || !m_running; });
            if (!m_running)
                break;
            buffer = m_buffer_queue.front();
            m_buffer_queue.pop_front();
            has_discontinuity = m_needs_discontinuity;
            m_needs_discontinuity = false;
        }

        if (has_discontinuity)
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);

        // gst_pad_push takes ownership of the buffer — do NOT unref after this call.
        GstFlowReturn push_result = gst_pad_push(m_src_pad, buffer);
        if (push_result != GST_FLOW_OK && push_result != GST_FLOW_FLUSHING)
        {
            GST_WARNING_OBJECT(m_owner_element, "BufferForwarder[%s]: gst_pad_push returned %s", m_stream_id.c_str(),
                               gst_flow_get_name(push_result));
        }
    }
    drain();
    GST_DEBUG_OBJECT(m_owner_element, "BufferForwarder[%s]: worker stopped", m_stream_id.c_str());
}
