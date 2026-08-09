#pragma once

#include <linux/videodev2.h>
#include <stddef.h>
#include <vector>
#include <algorithm>
#include <utility>

#include "dma_buffer.hpp"
#include "media_library_logger.hpp"

namespace HDR
{

class VideoBuffer
{
  public:
    static constexpr int MAX_NUM_OF_PLANES = 3;

  public:
    VideoBuffer();

  public:
    bool init(DMABufferAllocator &allocator, v4l2_buf_type fmt_type, size_t index, size_t planes, size_t plane_size,
              bool timestamp_copy, int v4l_fd);

    std::vector<int> get_planes()
    {
        std::vector<int> fds;
        for (unsigned int i = 0; i < m_num_planes; i++)
        {
            fds.push_back(m_dma_buffers[i].get_fd());
        }
        return fds;
    }

    std::vector<files_utils::SharedFd> get_planes_fds()
    {
        std::vector<files_utils::SharedFd> owners;
        for (unsigned int plane = 0; plane < m_num_planes; plane++)
        {
            owners.push_back(m_dma_buffers[plane].get_shared_fd());
        }
        return owners;
    }

    inline struct v4l2_buffer *get_v4l2_buffer()
    {
        return &m_v4l2_buffer;
    }

    void swap_plane_fds_with(VideoBuffer &other)
    {
        const unsigned int planes = std::min(m_num_planes, other.m_num_planes);
        for (unsigned int i = 0; i < planes; i++)
        {
            std::swap(m_dma_buffers[i], other.m_dma_buffers[i]);
            std::swap(m_v4l2_buffer.m.planes[i].m.fd, other.m_v4l2_buffer.m.planes[i].m.fd);
        }
    }

  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;
    unsigned int m_num_planes;
    DMABuffer m_dma_buffers[VideoBuffer::MAX_NUM_OF_PLANES];
    struct v4l2_plane m_v4l2_planes[VideoBuffer::MAX_NUM_OF_PLANES];
    struct v4l2_buffer m_v4l2_buffer;
};

} // namespace HDR
