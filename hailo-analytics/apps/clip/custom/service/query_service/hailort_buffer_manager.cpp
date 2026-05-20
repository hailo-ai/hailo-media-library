#include "hailort_buffer_manager.hpp"

#include <hailort.h>
#include <exception>

#include "common_utils.hpp"

using namespace hailort;

HailortBufferManager::BufferPair::BufferPair(std::shared_ptr<uint8_t> buf, DmaMappedBuffer map)
    : buffer(buf), mapping(std::move(map))
{
}

size_t HailortBufferManager::get_buffer_size() const
{
    return buffer_size_;
}

size_t HailortBufferManager::get_max_buffers() const
{
    return max_buffers_;
}

HailortBufferManager::HailortBufferManager(std::shared_ptr<VDevice> vdevice, size_t buffer_size, size_t max_buffers,
                                           BufferType type)
    : buffer_size_(buffer_size), max_buffers_(max_buffers), buffer_type_(type), vdevice_(vdevice),
      shared_state_(std::make_shared<SharedState>())
{

    all_buffers_.reserve(max_buffers_);
}

HailortBufferManager::~HailortBufferManager()
{
    if (shared_state_)
    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->manager_alive = false;
        // Clear the queue since manager is going away
        std::queue<size_t> empty;
        shared_state_->available_indices.swap(empty);
    }

    // all_buffers_ will be automatically destroyed, which will destroy
    // the DMA mappings and release the original buffer shared_ptrs
}

bool HailortBufferManager::is_buffer_available() const
{
    if (!shared_state_)
        return false;

    std::lock_guard<std::mutex> lock(shared_state_->mutex);

    if (!shared_state_->manager_alive)
        return false;

    // Check if we have pre-allocated available buffers
    if (!shared_state_->available_indices.empty())
    {
        return true;
    }

    // Check if we can allocate new buffers
    return all_buffers_.size() < max_buffers_;
}

std::shared_ptr<uint8_t> HailortBufferManager::get_buffer()
{
    if (!shared_state_)
        return nullptr;

    std::lock_guard<std::mutex> lock(shared_state_->mutex);

    if (!shared_state_->manager_alive)
        return nullptr;

    // Try to reuse existing available buffer
    if (!shared_state_->available_indices.empty())
    {
        size_t index = shared_state_->available_indices.front();
        shared_state_->available_indices.pop();

        return create_managed_buffer(index);
    }

    // Allocate new buffer if we haven't reached the limit
    if (all_buffers_.size() < max_buffers_)
    {
        return allocate_new_buffer();
    }

    // No buffers available
    return nullptr;
}

HailortBufferManager::Stats HailortBufferManager::get_stats() const
{
    Stats stats{};
    stats.buffer_size = buffer_size_;
    stats.max_buffers = max_buffers_;
    stats.buffer_type = buffer_type_;

    if (!shared_state_)
    {
        stats.manager_alive = false;
        return stats;
    }

    std::lock_guard<std::mutex> lock(shared_state_->mutex);

    stats.manager_alive = shared_state_->manager_alive;
    stats.total_allocated = all_buffers_.size();
    stats.currently_available = shared_state_->available_indices.size();
    stats.currently_in_use = stats.total_allocated - stats.currently_available;

    return stats;
}

std::shared_ptr<uint8_t> HailortBufferManager::allocate_new_buffer()
{
    try
    {
        // Allocate page-aligned buffer
        auto buffer = SystemUtils::page_aligned_alloc(buffer_size_);
        if (!buffer)
        {
            return nullptr;
        }

        // Create DMA mapping
        hailo_dma_buffer_direction_t direction =
            (buffer_type_ == BufferType::INPUT) ? HAILO_DMA_BUFFER_DIRECTION_H2D : HAILO_DMA_BUFFER_DIRECTION_D2H;
        auto mapping = DmaMappedBuffer::create(*vdevice_, buffer.get(), buffer_size_, direction)
                           .expect("Failed to map buffer to VDevice");

        // Store the buffer pair
        size_t index = all_buffers_.size();
        all_buffers_.emplace_back(buffer, std::move(mapping));

        return create_managed_buffer(index);
    }
    catch (const std::exception &e)
    {
        // Log error if needed
        return nullptr;
    }
}

std::shared_ptr<uint8_t> HailortBufferManager::create_managed_buffer(size_t index)
{
    auto &buffer_pair = all_buffers_[index];

    // Capture shared_state and original buffer pointer
    auto shared_state = shared_state_;      // Keep shared state alive
    auto original_ptr = buffer_pair.buffer; // Keep original buffer alive

    // Create a shared_ptr with custom deleter that safely handles cleanup
    return std::shared_ptr<uint8_t>(buffer_pair.buffer.get(), [shared_state, original_ptr, index](void *) mutable {
        // When user releases their reference, safely try to return to pool
        if (shared_state)
        {
            std::lock_guard<std::mutex> lock(shared_state->mutex);

            // Only return to pool if manager is still alive and this was the last user reference
            if (shared_state->manager_alive && original_ptr.use_count() == 2)
            {
                // Reference count is 2: our original_ptr + the BufferPair's buffer
                shared_state->available_indices.push(index);
            }
        }

        // Release our references - this may trigger buffer destruction
        // if manager is gone and this was the last reference
        original_ptr.reset();
    });
}
