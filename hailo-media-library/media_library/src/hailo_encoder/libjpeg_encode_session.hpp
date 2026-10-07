/*
 * Copyright (c) 2017-2026 Hailo Technologies Ltd. All rights reserved.
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
 */
#pragma once

#include <cstdint>
// jpeglib.h uses size_t and FILE* in its signatures but does not include their headers.
#include <stddef.h> // IWYU pragma: keep
#include <stdio.h>  // IWYU pragma: keep
#include <jpeglib.h>
#include <setjmp.h>
#include <string>
#include <vector>

#include <tl/expected.hpp>

#include "media_library/media_library_types.hpp"

/**
 * @file libjpeg_encode_session.hpp
 * @brief LibJpegEncodeSession — RAII wrapper over the libjpeg compression API.
 *        Reusable across frames; designed to be held long-lived per worker
 *        thread.
 *
 * Owns the jpeg_compress_struct lifecycle (constructed once in the ctor and
 * reused for every frame via jpeg_start_compress / jpeg_finish_compress),
 * the error trampoline, the chroma deinterleave scratch buffers (resized only
 * when frame dimensions change -- gst jpegenc's "resync"), and the in-memory
 * output buffer that jpeg_mem_dest allocates and grows.
 *
 * Not thread-safe: jpeg_compress_struct is single-threaded. Use one session
 * per thread.
 */

class LibJpegEncodeSession
{
  public:
    LibJpegEncodeSession();
    ~LibJpegEncodeSession();

    LibJpegEncodeSession(const LibJpegEncodeSession &) = delete;
    LibJpegEncodeSession &operator=(const LibJpegEncodeSession &) = delete;

    /**
     * Encode an NV12 frame as a baseline 4:2:0 JPEG. Returns the encoded
     * bitstream on success, or a media_library_return error code on failure
     * (and logs the libjpeg error message).
     *
     * Uses libjpeg's raw_data mode -- the Y plane is read directly with zero
     * copy and the UV plane is deinterleaved one MCU row at a time into small
     * scratch buffers, matching the behaviour of GStreamer's jpegenc.
     *
     * @param y_plane   Pointer to the Y luma plane (height rows of y_stride bytes).
     * @param y_stride  Distance in bytes between adjacent Y rows.
     * @param uv_plane  Pointer to the interleaved UV plane (height/2 rows of uv_stride bytes).
     * @param uv_stride Distance in bytes between adjacent UV rows.
     * @param width     Frame width in pixels. Must be even.
     * @param height    Frame height in pixels.
     * @param quality   JPEG quality 1..100.
     */
    tl::expected<std::vector<std::uint8_t>, media_library_return> encode_nv12(const std::uint8_t *y_plane, int y_stride,
                                                                              const std::uint8_t *uv_plane,
                                                                              int uv_stride, int width, int height,
                                                                              int quality);

  private:
    struct ErrorManager
    {
        struct jpeg_error_mgr pub;
        jmp_buf escape;
        std::string message;
    };

    static void capture_error(j_common_ptr cinfo);

    jpeg_compress_struct m_cinfo{};
    ErrorManager m_jerr{};
    unsigned char *m_jpeg_buf = nullptr;
    unsigned long m_jpeg_size = 0;
    // Allocation size of m_jpeg_buf. jpeg_mem_dest receives this (not the previous
    // frame's byte count) so same-size frames never force libjpeg to grow the buffer.
    unsigned long m_jpeg_capacity = 0;

    // Chroma deinterleave scratch -- u/v planes are laid out 8-rows-high
    // (one MCU row) by chroma_width = frame_width/2 columns. Resized only on
    // dimension change, same as gst jpegenc's resync() / set_format().
    std::vector<std::uint8_t> m_u_scratch;
    std::vector<std::uint8_t> m_v_scratch;
    int m_cached_width = -1;
    int m_cached_height = -1;
    int m_cached_quality = -1; // forces jpeg_set_defaults + jpeg_set_quality on first encode
};
