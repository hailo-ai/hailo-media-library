#include "video_buffer.hpp"
#include "logger_macros.hpp"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

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

VideoBuffer::~VideoBuffer()
{
    destroy();
}

bool VideoBuffer::init(DMABufferAllocator &allocator, v4l2_buf_type fmt_type, size_t index, size_t planes,
                       size_t plane_size, bool timestamp_copy)
{
    m_num_planes = planes;

    m_v4l2_buffer.type = fmt_type;
    m_v4l2_buffer.memory = V4L2_MEMORY_DMABUF;
    m_v4l2_buffer.index = index;
    m_v4l2_buffer.length = planes;
    m_v4l2_buffer.m.planes = m_v4l2_planes;

    if (timestamp_copy)
        m_v4l2_buffer.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

    for (unsigned int plane = 0; plane < planes; ++plane)
    {
        DMABuffer dma_buf;

        if (!allocator.alloc(plane_size, dma_buf))
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to allocate DMA buffer of size {}", plane_size);
            return false;
        }

        if (dma_buf.m_fd <= 0)
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "DMA buffer fd is invalid");
            return false;
        }

        m_plane_fds[plane] = dma_buf.m_fd;
        m_v4l2_buffer.m.planes[plane].m.fd = dma_buf.m_fd;
        m_v4l2_buffer.m.planes[plane].length = dma_buf.m_size;
    }

    return true;
}

void VideoBuffer::destroy()
{
    for (unsigned int plane = 0; plane < m_num_planes; ++plane)
    {
        // Free the DMA buffer
        close(m_plane_fds[plane]);
    }
}

} // namespace HDR
