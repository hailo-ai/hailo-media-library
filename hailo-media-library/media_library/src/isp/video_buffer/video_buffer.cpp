#include "video_buffer.hpp"
#include "logger_macros.hpp"

#include <cstddef>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/videodev2.h>

namespace HDR
{

/***
 * Video Buffer
 ***/

VideoBuffer::VideoBuffer()
{
    m_num_planes = 0;
    memset(m_plane_fds, 0, sizeof(m_plane_fds));
    memset(m_v4l2_planes, 0, sizeof(m_v4l2_planes));
    memset(&m_v4l2_buffer, 0, sizeof(m_v4l2_buffer));
}

bool VideoBuffer::init(DMABufferAllocator &allocator, v4l2_buf_type fmt_type, size_t index, size_t planes,
                       size_t plane_size, bool timestamp_copy, int v4l_fd)
{
    DMABuffer dma_bufs[VideoBuffer::MAX_NUM_OF_PLANES];

    for (unsigned int plane = 0; plane < planes; ++plane)
    {
        if (!allocator.alloc(plane_size, dma_bufs[plane]))
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate DMA buffer of size {}", plane_size);
            return false;
        }
    }

    m_num_planes = planes;

    m_v4l2_buffer.type = fmt_type;
    m_v4l2_buffer.memory = V4L2_MEMORY_DMABUF;
    m_v4l2_buffer.index = index;
    m_v4l2_buffer.length = planes;
    m_v4l2_buffer.m.planes = m_v4l2_planes;

    if (timestamp_copy)
        m_v4l2_buffer.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
    else
        m_v4l2_buffer.flags = 0;

    // send QUERYBUF ioctl
    if (ioctl(v4l_fd, VIDIOC_QUERYBUF, &m_v4l2_buffer) != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "VIDIOC_QUERYBUF failed with error: {}", errno);
        return false;
    }

    for (unsigned int plane = 0; plane < planes; ++plane)
    {
        m_plane_fds[plane] = dma_bufs[plane].get_fd();
        m_v4l2_buffer.m.planes[plane].m.fd = dma_bufs[plane].get_fd();
        m_v4l2_buffer.m.planes[plane].length = dma_bufs[plane].m_size;
    }
    for (unsigned int plane = 0; plane < VideoBuffer::MAX_NUM_OF_PLANES; ++plane)
    {
        m_dma_buffers[plane] = std::move(dma_bufs[plane]);
    }
    return true;
}

} // namespace HDR
