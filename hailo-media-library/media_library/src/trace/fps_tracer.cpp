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

#include "fps_tracer.hpp"
#include <chrono>

FpsTracer::FpsTracer(std::chrono::milliseconds window_duration) : m_window_duration(window_duration)
{
    m_window_start_time = std::chrono::steady_clock::now();
    m_last_report_time = m_window_start_time;
}

void FpsTracer::record_frame()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_frame_count++;

    auto current_time = std::chrono::steady_clock::now();

    if (current_time - m_last_report_time >= m_window_duration)
    {
        reset_window_and_report(current_time);
    }
}

void FpsTracer::reset_window_and_report(const std::chrono::steady_clock::time_point &current_time)
{
    auto elapsed = current_time - m_window_start_time;

    // Calculate FPS: frames / (elapsed_time_in_seconds)
    auto elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    double fps = m_frame_count / elapsed_seconds;

    // Call virtual method for derived class to report
    report_fps(fps);

    // Reset window
    m_window_start_time = current_time;
    m_last_report_time = current_time;
    m_frame_count = 0;
}
