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
#include <iostream>
#include <memory>
#include <vector>

#include "encoder_config.hpp"
#include "buffer_pool.hpp"
#include "media_library_types.hpp"

struct EncoderInputPlane
{
    void * data;
    uint32_t size;
    EncoderInputPlane(void * data, uint32_t size) : data(data), size(size) {};
};

struct EncoderOutputBuffer
{
    HailoMediaLibraryBufferPtr buffer;
    uint32_t size;
};

struct EncoderInputBuffer
{
    std::vector<EncoderInputPlane> m_planes;
    EncoderInputBuffer() {};
    void add_plane(EncoderInputPlane plane)
    {
        m_planes.emplace_back(std::move(plane));
    }
};

class Encoder {
public:
    Encoder(std::string json_path);
    ~Encoder();
    int get_gop_size();
    void force_keyframe();
    std::shared_ptr<EncoderConfig> get_config();
    std::vector<EncoderOutputBuffer> handle_frame(EncoderInputBuffer buf);
    EncoderOutputBuffer start();
    EncoderOutputBuffer stop();
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};