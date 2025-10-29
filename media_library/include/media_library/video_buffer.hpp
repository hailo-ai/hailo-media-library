#pragma once

#include <linux/v4l2-subdev.h>
#include <linux/types.h>

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

    inline int *get_planes()
    {
        return m_plane_fds;
    }

    inline struct v4l2_buffer *get_v4l2_buffer()
    {
        return &m_v4l2_buffer;
    }

  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;
    unsigned int m_num_planes;
    DMABuffer m_dma_buffers[VideoBuffer::MAX_NUM_OF_PLANES];
    int m_plane_fds[VideoBuffer::MAX_NUM_OF_PLANES];
    struct v4l2_plane m_v4l2_planes[VideoBuffer::MAX_NUM_OF_PLANES];
    struct v4l2_buffer m_v4l2_buffer;
};

} // namespace HDR
