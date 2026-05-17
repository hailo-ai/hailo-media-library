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
#include <stdint.h>
#include <tl/expected.hpp>
#include <memory>
#include <string>
#include <vector>

#include "encoder_class.hpp"
#include "encoder_internal.hpp"
#include "buffer_pool.hpp"
#include "media_library_types.hpp"

Encoder::Encoder()
{
    m_impl = std::make_unique<Impl>();
}

Encoder::~Encoder() = default;

media_library_return Encoder::release()
{
    return m_impl->release();
}

media_library_return Encoder::dispose()
{
    return m_impl->dispose();
}

media_library_return Encoder::init()
{
    return m_impl->init();
}

EncoderOutputBuffer Encoder::get_encoder_header_output_buffer()
{
    return m_impl->get_encoder_header_output_buffer();
}

void Encoder::update_stride(uint32_t stride)
{
    m_impl->update_stride(stride);
}

int Encoder::get_gop_size()
{
    return m_impl->get_gop_size();
}

void Encoder::force_keyframe()
{
    m_impl->force_keyframe();
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::start()
{
    return m_impl->start();
}

void Encoder::stop()
{
    m_impl->stop();
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::finish()
{
    return m_impl->finish();
}

std::vector<EncoderOutputBuffer> Encoder::handle_frame(const HailoMediaLibraryBufferPtr &buf, uint32_t frame_number)
{
    return m_impl->handle_frame(buf, frame_number);
}

void Encoder::set_stream_id(const std::string &stream_id)
{
    m_impl->set_stream_id(stream_id);
}

encoder_monitors Encoder::get_monitors()
{
    return m_impl->get_monitors();
}

void Encoder::set_start_callback(EncoderStartCallback callback)
{
    m_impl->set_start_callback(std::move(callback));
}
