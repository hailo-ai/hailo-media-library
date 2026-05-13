#include "snapshot.hpp"
#include "media_library_logger.hpp"

#include <fstream>

#include "dma_memory_allocator.hpp"
#include "logger_macros.hpp"

#define MODULE_NAME LoggerType::Snapshot

struct ScopedDmaMapping
{
    void *ptr = nullptr;

    bool map(size_t size, int fd)
    {
        if (DmaMemoryAllocator::get_instance().map_external_dma_buffer(size, fd, &ptr) != MEDIA_LIBRARY_SUCCESS)
            return false;
        DmaMemoryAllocator::get_instance().dmabuf_sync_start(ptr);
        return true;
    }

    ~ScopedDmaMapping()
    {
        if (ptr)
        {
            DmaMemoryAllocator::get_instance().dmabuf_sync_end(ptr);
            DmaMemoryAllocator::get_instance().unmap_external_dma_buffer(ptr);
        }
    }

    ScopedDmaMapping() = default;
    ScopedDmaMapping(const ScopedDmaMapping &) = delete;
    ScopedDmaMapping &operator=(const ScopedDmaMapping &) = delete;
};

bool SnapshotManager::save_medialib_buffer(const HailoMediaLibraryBufferPtr &buffer, const std::string &file_path)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Saving buffer to: {}", file_path);

    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open file: {}", file_path);
        return false;
    }

    size_t total_size = 0;
    for (size_t i = 0; i < buffer->buffer_data->planes_count; i++)
    {
        void *plane_ptr = buffer->get_plane_ptr(i);
        size_t plane_size = buffer->get_plane_size(i);
        int fd = buffer->get_plane_fd(i);
        ScopedDmaMapping mapping;

        // Detect DMA-BUF by fd rather than memory type — some buffers
        // (e.g. HailoRT inference outputs) carry valid fds without setting
        // HAILO_MEMORY_TYPE_DMABUF.
        if (fd >= 0)
        {
            if (plane_size == 0)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Plane {} has fd {} but zero size", i, fd);
                return false;
            }
            if (!mapping.map(plane_size, fd))
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to mmap DMA-BUF fd {} for plane {}", fd, i);
                return false;
            }
            plane_ptr = mapping.ptr;
        }

        if (!plane_ptr)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Null pointer for plane {} (non-DMA-BUF)", i);
            return false;
        }

        out.write(static_cast<const char *>(plane_ptr), plane_size);
        total_size += plane_size;
    }

    if (!out)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write buffer to file: {}", file_path);
        return false;
    }

    LOGGER__MODULE__INFO(MODULE_NAME, "Saved {} bytes to {}", total_size, file_path);
    return true;
}

void SnapshotManager::process_snapshot_request(const SnapshotRequest &request)
{
    bool success = save_medialib_buffer(request.buffer, request.file_path);

    if (!success)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to write buffer to file for stage '{}'.", request.stage_name);
    }
    else
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Successfully saved snapshot for stage '{}'.", request.stage_name);
    }

    // Decrement the pending operations counter and check if we need to process frame completion
    int pending = m_pending_operations.fetch_sub(1) - 1;

    if (pending == 0)
    {
        // This was the last pending operation, check if we need to process frame completion
        std::unique_lock<std::mutex> lock(m_completion_mutex);
        if (m_frame_complete)
        {
            m_frame_complete = false;
            process_snapshot_frame_complete();
        }
    }
}
