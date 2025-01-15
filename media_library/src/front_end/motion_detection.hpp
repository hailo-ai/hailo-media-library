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
 * @file motion_detection_.hpp
 * @brief MediaLibrary Motion Detection CPP API module
 **/

#pragma once
#include "media_library_types.hpp"
class MotionDetection
{
  public:
    MotionDetection();
    MotionDetection(motion_detection_config_t &motion_detection_config);

    media_library_return perform_motion_detection(std::vector<HailoMediaLibraryBufferPtr> &output_frames);
    media_library_return allocate_motion_detection(uint32_t max_buffer_pool_size);

  private:
    bool is_frame_valid(const HailoMediaLibraryBufferPtr &buffer_ptr) const;
    bool initialize_previous_frame(const HailoMediaLibraryBufferPtr &buffer_ptr);
    void create_current_frame(const HailoMediaLibraryBufferPtr &buffer_ptr);
    HailoMediaLibraryBufferPtr allocate_bitmask_buffer();
    void create_motion_mask();
    bool detect_motion() const;
    void update_output_frames(std::vector<HailoMediaLibraryBufferPtr> &output_frames,
                              HailoMediaLibraryBufferPtr &bitmask_buffer, bool motion_detected);
    void update_previous_frame(const HailoMediaLibraryBufferPtr &current_buffer_ptr);
    void log_execution_time(const timespec &start, const timespec &end) const;

    motion_detection_config_t m_motion_detection_config;
    // motion detection output resolution
    output_resolution_t m_motion_detection_output_resolution;

    MediaLibraryBufferPoolPtr m_motion_detection_buffer_pool;
    // Previous buffer for motion detection
    HailoMediaLibraryBufferPtr m_motion_detection_previous_buffer_ptr;
    // Previous frame for motion detection
    cv::Mat m_motion_detection_previous_frame;
    // Current buffer for motion detection
    cv::Mat m_motion_detection_current_frame;

    cv::Mat m_motion_detection_mask;
    cv::Rect m_motion_detection_roi;
};
