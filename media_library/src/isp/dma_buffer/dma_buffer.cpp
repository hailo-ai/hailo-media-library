#include "dma_buffer.hpp"

#include <fcntl.h>
#include <limits>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-heap.h>
#include <linux/types.h>
#include <string.h>

namespace HDR
{

DMABuffer::DMABuffer() : m_size(0), m_ptr(nullptr), m_fd(nullptr)
{
}

void DMABuffer::init(files_utils::SharedFd fd, size_t size)
{
    m_fd = fd;
    m_size = size;
}

bool DMABuffer::initialized()
{
    return m_fd && m_size > 0;
}

bool DMABuffer::map()
{
    if (!initialized())
    {
        return false;
    }

    m_ptr = mmap(NULL, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, *m_fd, 0);
    if (m_ptr == MAP_FAILED)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "mmap failed with errno: {} ({})", errno, strerror(errno));
        m_ptr = nullptr;
        return false;
    }

    return true;
}

void *DMABuffer::ptr()
{
    if (m_ptr == nullptr)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Error: Buffer not mapped yet!");
        return nullptr;
    }
    return m_ptr;
}

void DMABuffer::unmap()
{
    if (m_ptr != nullptr && m_size > 0)
    {
        if (munmap(m_ptr, m_size) != 0)
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "munmap failed with errno: {} ({})", errno, strerror(errno));
        }
        m_ptr = nullptr;
    }
}

int DMABuffer::get_fd()
{
    if (!m_fd)
    {
        return -1;
    }
    return *m_fd;
}

DMABuffer::~DMABuffer()
{
    unmap();
}

bool DMABufferAllocator::init(const char *name)
{
    return open_dma_heap_device(name);
}

bool DMABufferAllocator::alloc(size_t size, DMABuffer &o_dma_buffer)
{
    struct dma_heap_allocation_data heap_data = {.len = size, .fd = 0, .fd_flags = O_RDWR | O_CLOEXEC, .heap_flags = 0};

    if (!m_heap_fd)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "DMA heap device not initialized");
        return false;
    }
    if (ioctl(*m_heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data) != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "DMA heap allocation failed with errno: {} ({}). size {}", errno,
                              strerror(errno), size);
        return false;
    }

    if (heap_data.fd > std::numeric_limits<int32_t>::max())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "DMA heap allocation returned invalid file descriptor: {}", heap_data.fd);
        return false;
    }

    if (heap_data.len != size)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "DMA heap allocation returned size {} instead of requested size {}",
                              heap_data.len, size);
        close(heap_data.fd);
        return false;
    }

    o_dma_buffer.init(files_utils::make_shared_fd((int)heap_data.fd), heap_data.len);
    return true;
}

bool DMABufferAllocator::open_dma_heap_device(const char *name)
{
    int fd = open(name, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Failed to open DMA heap: {}", name);
        return false;
    }

    m_heap_fd = files_utils::make_shared_fd(fd);
    return true;
}

} // namespace HDR
