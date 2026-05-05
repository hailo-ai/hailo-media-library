#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <optional>
#include <atomic>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <sstream>

#include "hailo/hailort.hpp"
#include "common_utils.hpp"

using namespace hailort;

class HailortBufferManager
{

  public:
    enum BufferType
    {
        INPUT,
        OUTPUT
    };

  private:
    struct BufferPair
    {
        std::shared_ptr<uint8_t> buffer; // From page_aligned_alloc
        DmaMappedBuffer mapping;         // DMA mapped buffer

        BufferPair(std::shared_ptr<uint8_t> buf, DmaMappedBuffer map);

        // Move-only semantics
        BufferPair(const BufferPair &) = delete;
        BufferPair &operator=(const BufferPair &) = delete;
        BufferPair(BufferPair &&) = default;
        BufferPair &operator=(BufferPair &&) = default;
    };

    // Shared state that outlives the HailortBufferManager if needed
    struct SharedState
    {
        std::queue<size_t> available_indices;
        std::mutex mutex;
        std::atomic<bool> manager_alive{true};

        ~SharedState() = default;
    };

    const size_t buffer_size_;
    const size_t max_buffers_;
    BufferType buffer_type_;
    std::shared_ptr<VDevice> vdevice_;

    std::vector<BufferPair> all_buffers_;       // All allocated buffer pairs
    std::shared_ptr<SharedState> shared_state_; // Shared with outstanding buffers

  public:
    /**
     * Constructor
     * @param vdevice Reference to Hailo VDevice for DMA mapping
     * @param buffer_size Size of each buffer (fixed)
     * @param max_buffers Maximum number of buffers to allocate
     */
    HailortBufferManager(std::shared_ptr<VDevice> vdevice, size_t buffer_size, size_t max_buffers, BufferType type);

    /**
     * Destructor - marks manager as destroyed and cleans up
     */
    ~HailortBufferManager();

    /**
     * Check if there's at least one buffer available
     * @return true if buffer is available, false otherwise
     */
    bool is_buffer_available() const;

    /**
     * Get an available buffer
     * @return shared_ptr to buffer data, or nullptr if no buffer available
     *
     * The returned shared_ptr allows direct access to buffer data.
     * The HailortBufferManager tracks usage through reference counting.
     * User should not hold onto the shared_ptr longer than necessary.
     */
    std::shared_ptr<uint8_t> get_buffer();

    /**
     * Get current statistics
     */
    struct Stats
    {
        size_t total_allocated;
        size_t currently_available;
        size_t currently_in_use;
        size_t buffer_size;
        size_t max_buffers;
        BufferType buffer_type;
        bool manager_alive;
    };

    Stats get_stats() const;

    /**
     * Get the fixed buffer size
     */
    size_t get_buffer_size() const;

    /**
     * Get maximum number of buffers
     */
    size_t get_max_buffers() const;

  private:
    /**
     * Allocate a new buffer pair and return managed shared_ptr
     */
    std::shared_ptr<uint8_t> allocate_new_buffer();

    /**
     * Create a managed shared_ptr that safely handles manager destruction
     */
    std::shared_ptr<uint8_t> create_managed_buffer(size_t index);
};
