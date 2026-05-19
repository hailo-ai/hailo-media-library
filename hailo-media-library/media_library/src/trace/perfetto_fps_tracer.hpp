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

#include <string>
#include <chrono>

#include "fps_tracer.hpp"

#ifdef HAVE_PERFETTO
#include "hailo_media_library_perfetto.hpp"
#endif

/**
 * @brief Perfetto-specific FPS tracer implementation
 *
 * Reports FPS metrics to Perfetto tracing system using counter tracks.
 * Creates a dynamic counter track once in the constructor and reuses it
 * for all FPS reports.
 */
class PerfettoFpsTracer : public FpsTracer
{
  public:
    /**
     * @brief Construct Perfetto FPS tracer
     * @param track_name Name for the Perfetto counter track (e.g., "Dewarp FPS")
     * @param window_duration Time window for FPS calculation (default: 1000ms)
     */
    explicit PerfettoFpsTracer(const std::string &track_name,
                               std::chrono::milliseconds window_duration = std::chrono::milliseconds(1000));

  protected:
    /**
     * @brief Report FPS to Perfetto
     * @param fps The calculated frames per second for the completed window
     */
    void report_fps(double fps) override;

  private:
#ifdef HAVE_PERFETTO
    std::string m_counter_name;
    perfetto::CounterTrack m_counter_track;
#endif
};
