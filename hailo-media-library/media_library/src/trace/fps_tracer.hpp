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

#include <stddef.h>
#include <stdint.h>
#include <chrono>
#include <mutex>

/**
 * @brief Base class for FPS tracing with windowed calculation
 *
 * This class accumulates frame counts over time windows and calculates average FPS.
 * Subclasses can override report_fps() to implement different reporting mechanisms
 * (e.g., Perfetto, logs, metrics).
 *
 * Thread-safe for concurrent record_frame() calls from multiple threads.
 */
class FpsTracer
{
  public:
    /**
     * @brief Construct FPS tracer
     * @param window_duration Time window for FPS calculation (default: 1000ms)
     */
    explicit FpsTracer(std::chrono::milliseconds window_duration = std::chrono::milliseconds(1000));

    virtual ~FpsTracer() = default;

    /**
     * @brief Record a frame processed
     *
     * Thread-safe, can be called from any thread.
     * Automatically calls report_fps() when window duration expires.
     */
    void record_frame();

  protected:
    /**
     * @brief Report FPS to the chosen destination
     *
     * Override this method to implement custom reporting (Perfetto, logs, etc.)
     * Called automatically when a time window completes.
     *
     * @param fps The calculated frames per second for the completed window
     */
    virtual void report_fps(double fps) = 0;

  private:
    std::chrono::milliseconds m_window_duration;

    // For thread-safe FPS calculation
    std::mutex m_mutex;
    size_t m_frame_count{0};
    std::chrono::steady_clock::time_point m_window_start_time;
    std::chrono::steady_clock::time_point m_last_report_time;

    /**
     * @brief Reset window and report FPS
     * @param current_time Current time point
     */
    void reset_window_and_report(const std::chrono::steady_clock::time_point &current_time);
};
