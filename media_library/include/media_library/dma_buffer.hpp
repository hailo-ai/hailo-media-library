#pragma once

#include <cstddef>

#include "files_utils.hpp"
#include "media_library_logger.hpp"

namespace HDR
{

struct DMABuffer
{
  public:
    size_t m_size;

    DMABuffer();
    void init(files_utils::SharedFd fd, size_t size);
    bool initialized();
    bool map();
    void *ptr();
    void unmap();
    int get_fd();
    ~DMABuffer();

  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;
    void *m_ptr; // Only accessible through ptr() after map()
    files_utils::SharedFd m_fd;
};

class DMABufferAllocator
{
  public:
    bool init(const char *name);
    bool alloc(size_t size, DMABuffer &o_dma_buffer);

  private:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;
    bool open_dma_heap_device(const char *name);
    files_utils::SharedFd m_heap_fd;
};

} // namespace HDR
