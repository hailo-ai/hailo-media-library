#pragma once

#include <cstddef>

#include "media_library_logger.hpp"

namespace HDR
{

struct DMABuffer
{
  public:
    int m_fd;
    size_t m_size;

    DMABuffer();
    void init(int fd, size_t size);
    bool initialized();
    bool map();
    void *ptr();
    void unmap();
    ~DMABuffer();

  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;
    void *m_ptr; // Only accessible through ptr() after map()
};

class DMABufferAllocator
{
  public:
    virtual ~DMABufferAllocator();
    DMABufferAllocator();
    bool init(const char *name);
    bool alloc(size_t size, DMABuffer &o_dma_buffer);

  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;
    bool open_dma_heap_device(const char *name);
    void close_dma_heap_device();
    int m_heap_fd;
};

} // namespace HDR
