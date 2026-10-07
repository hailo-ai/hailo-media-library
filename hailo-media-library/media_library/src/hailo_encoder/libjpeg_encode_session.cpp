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

#include "libjpeg_encode_session.hpp"

#include <jmorecfg.h>
#include <algorithm>
#include <cstdlib>

#include "media_library/media_library_logger.hpp"
#include "media_library_types.hpp"

static constexpr LoggerType MODULE_NAME = LoggerType::Encoder;

// One MCU row of 4:2:0 = 16 luma scanlines + 8 chroma scanlines.
static constexpr int MCU_ROWS_Y = 16;
static constexpr int MCU_ROWS_UV = 8;

void LibJpegEncodeSession::capture_error(j_common_ptr cinfo)
{
    auto *err = reinterpret_cast<ErrorManager *>(cinfo->err);
    char buf[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buf);
    err->message = buf;
    longjmp(err->escape, 1);
}

LibJpegEncodeSession::LibJpegEncodeSession()
{
    m_cinfo.err = jpeg_std_error(&m_jerr.pub);
    m_jerr.pub.error_exit = capture_error;
    jpeg_create_compress(&m_cinfo);
}

LibJpegEncodeSession::~LibJpegEncodeSession()
{
    jpeg_destroy_compress(&m_cinfo);
    if (m_jpeg_buf != nullptr)
    {
        std::free(m_jpeg_buf);
    }
}

// Point the 16 luma row-pointers of one MCU row straight at the source Y plane (no copy).
// Rows past the bottom edge clamp to the last real row so libjpeg never reads out of bounds;
// decoders drop them because the bitstream stores the true image height.
static void map_luma_rows(const std::uint8_t *y_plane, int y_stride, int height, int first_row, JSAMPROW *y_rows)
{
    for (int row = 0; row < MCU_ROWS_Y; ++row)
    {
        const int src_row = std::min(first_row + row, height - 1);
        y_rows[row] = const_cast<JSAMPROW>(y_plane + static_cast<size_t>(src_row) * y_stride);
    }
}

// NV12 packs chroma as interleaved U,V,U,V... but libjpeg wants U and V as two separate planes.
// De-interleave the 8 chroma rows of one MCU row into the u/v scratch buffers and point
// u_rows/v_rows at them. Rows past the bottom edge clamp to last_row, same as the luma rows.
static void split_chroma_rows(const std::uint8_t *uv_plane, int uv_stride, int first_row, int last_row,
                              int chroma_width, std::uint8_t *u_scratch, std::uint8_t *v_scratch, JSAMPROW *u_rows,
                              JSAMPROW *v_rows)
{
    for (int row = 0; row < MCU_ROWS_UV; ++row)
    {
        const int src_row = std::min(first_row + row, last_row);
        const std::uint8_t *interleaved = uv_plane + static_cast<size_t>(src_row) * uv_stride;
        std::uint8_t *u_dst = u_scratch + static_cast<size_t>(row) * chroma_width;
        std::uint8_t *v_dst = v_scratch + static_cast<size_t>(row) * chroma_width;
        for (int col = 0; col < chroma_width; ++col)
        {
            u_dst[col] = interleaved[2 * col];
            v_dst[col] = interleaved[2 * col + 1];
        }
        u_rows[row] = u_dst;
        v_rows[row] = v_dst;
    }
}

// libjpeg wants the three colour channels (YUV) as *separate* row-pointer lists, and
// it processes the image 16 pixel-rows at a time (one "MCU row"). For each MCU
// row we:
//   1. Point 16 Y row-pointers straight at the source Y plane -- no copying.
//   2. Deinterleave 8 rows of packed U,V,U,V... into a U scratch and a V scratch
//      (libjpeg needs them as two separate planes). The scratch holds only one
//      MCU row at a time, but every chroma byte in the whole frame is copied
//      across the encode -- this is the load-bearing cost of the deinterleave.
//   3. Hand those pointers to jpeg_write_raw_data, which does the actual JPEG
//      math (DCT + quantize + Huffman).
//
// libjpeg appends the compressed bytes into m_jpeg_buf as it goes. When we're
// done we copy that buffer into a std::vector and return it.
tl::expected<std::vector<std::uint8_t>, media_library_return> LibJpegEncodeSession::encode_nv12(
    const std::uint8_t *y_plane, int y_stride, const std::uint8_t *uv_plane, int uv_stride, int width, int height,
    int quality)
{
    // jpeg_mem_dest treats the passed-in buffer as caller-owned: when the compressed
    // frame outgrows it, libjpeg mallocs a replacement and never frees ours. Remember
    // the buffer we hand in so it can be freed if libjpeg swaps it out (volatile:
    // read after a potential longjmp).
    unsigned char *volatile buffer_before_encode = m_jpeg_buf;

    if (setjmp(m_jerr.escape) != 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "libjpeg error: {}", m_jerr.message);
        if (m_jpeg_buf != nullptr)
        {
            std::free(m_jpeg_buf);
        }
        if (buffer_before_encode != nullptr && buffer_before_encode != m_jpeg_buf)
        {
            std::free(buffer_before_encode);
        }
        m_jpeg_buf = nullptr;
        m_jpeg_size = 0;
        m_jpeg_capacity = 0;
        return tl::unexpected(MEDIA_LIBRARY_ERROR);
    }

    // Hand libjpeg the full allocation, not the previous frame's byte count —
    // otherwise every frame slightly larger than the last forces a grow.
    m_jpeg_size = m_jpeg_capacity;
    jpeg_mem_dest(&m_cinfo, &m_jpeg_buf, &m_jpeg_size);

    m_cinfo.image_width = static_cast<JDIMENSION>(width);
    m_cinfo.image_height = static_cast<JDIMENSION>(height);
    if (quality != m_cached_quality)
    {
        m_cinfo.input_components = 3;
        m_cinfo.in_color_space = JCS_YCbCr;
        jpeg_set_defaults(&m_cinfo);
        jpeg_set_quality(&m_cinfo, quality, TRUE);

        // raw_data mode lets us feed YCbCr planes directly and skip libjpeg's built-in colour conversion.
        m_cinfo.raw_data_in = TRUE;
        m_cinfo.comp_info[0].h_samp_factor = 2;
        m_cinfo.comp_info[0].v_samp_factor = 2;
        m_cinfo.comp_info[1].h_samp_factor = 1;
        m_cinfo.comp_info[1].v_samp_factor = 1;
        m_cinfo.comp_info[2].h_samp_factor = 1;
        m_cinfo.comp_info[2].v_samp_factor = 1;
        m_cached_quality = quality;
    }

    jpeg_start_compress(&m_cinfo, TRUE);

    const int chroma_width = width / 2;
    if (width != m_cached_width || height != m_cached_height)
    {
        // gst jpegenc's "resync" -- only grow the scratch when dimensions change.
        m_u_scratch.resize(static_cast<size_t>(MCU_ROWS_UV) * chroma_width);
        m_v_scratch.resize(static_cast<size_t>(MCU_ROWS_UV) * chroma_width);
        m_cached_width = width;
        m_cached_height = height;
    }

    JSAMPROW y_rows[MCU_ROWS_Y];
    JSAMPROW u_rows[MCU_ROWS_UV];
    JSAMPROW v_rows[MCU_ROWS_UV];
    JSAMPARRAY raw_planes[3] = {y_rows, u_rows, v_rows};

    // Feed the image to libjpeg one MCU row at a time. jpeg_write_raw_data advances next_scanline by 16.
    const int last_chroma_row = (height + 1) / 2 - 1;
    while (m_cinfo.next_scanline < static_cast<JDIMENSION>(height))
    {
        const int first_luma_row = static_cast<int>(m_cinfo.next_scanline);
        map_luma_rows(y_plane, y_stride, height, first_luma_row, y_rows);
        split_chroma_rows(uv_plane, uv_stride, first_luma_row / 2, last_chroma_row, chroma_width, m_u_scratch.data(),
                          m_v_scratch.data(), u_rows, v_rows);
        jpeg_write_raw_data(&m_cinfo, raw_planes, MCU_ROWS_Y);
    }

    jpeg_finish_compress(&m_cinfo);

    if (buffer_before_encode != nullptr && m_jpeg_buf != buffer_before_encode)
    {
        // libjpeg outgrew the buffer we handed in and replaced it; release the abandoned one.
        std::free(buffer_before_encode);
        m_jpeg_capacity = m_jpeg_size;
    }
    else if (m_jpeg_size > m_jpeg_capacity)
    {
        // First encode: libjpeg allocated the buffer itself.
        m_jpeg_capacity = m_jpeg_size;
    }

    return std::vector<std::uint8_t>(m_jpeg_buf, m_jpeg_buf + m_jpeg_size);
}
