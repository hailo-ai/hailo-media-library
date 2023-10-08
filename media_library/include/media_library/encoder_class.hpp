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