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

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <gst/gst.h>

/**
 * @brief Bounded queue + worker thread for forwarding GstBuffers to a src pad.
 *
 * Decouples a non-blocking callback (enqueue) from the potentially-blocking
 * gst_pad_push call. The queue uses a drop-oldest policy when full.
 *
 * Ownership contract:
 *  - enqueue() takes ownership of the buffer ref (caller must NOT unref after).
 *  - The worker thread transfers ownership to gst_pad_push (which always takes it).
 *  - Dropped or drained buffers are unreffed by the forwarder.
 */
class BufferForwarder
{
  public:
    BufferForwarder(GstPad *src_pad, std::string stream_id, GstElement *owner_element, size_t max_queue_size = 2);
    ~BufferForwarder();

    /** Non-blocking enqueue. Takes ownership of @p buffer. Drops oldest if full. */
    void enqueue(GstBuffer *buffer);

    /** Launch the worker thread. */
    void start();

    /** Signal shutdown, join worker thread, drain remaining buffers. */
    void stop();

  private:
    void drain();
    void worker_loop();

    GstPad *m_src_pad;
    std::string m_stream_id;
    GstElement *m_owner_element;

    std::deque<GstBuffer *> m_buffer_queue;
    size_t m_max_queue_size;
    std::mutex m_mutex;
    std::condition_variable m_buffer_available_cv;
    bool m_running = false;
    bool m_needs_discontinuity = false;
    std::thread m_worker_thread;
};
