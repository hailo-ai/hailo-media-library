#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace vlm_event_monitor
{

// Sliding-window cache of the most recent N JPEG-encoded frames produced by
// the VGA encoder. Lives at the tail of the encoder pipeline (no downstream
// subscribers).
//
// Buffers arriving from EncoderStage carry a SizeMetadata whose value is the
// encoded byte count; the JPEG bytes themselves are at plane 0
// (`buffer->get_plane_ptr(0)`) — see EncoderStage::create() and clip's
// thumb_storage_stage.cpp:225-237 for the same access pattern.
class JpegRingBufferStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    static constexpr size_t kDefaultCapacity = 10;

    JpegRingBufferStage(std::string name, size_t capacity = kDefaultCapacity, size_t queue_size = 1, bool leaky = true,
                        bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;
    hailo_analytics::pipeline::AppStatus deinit() override;
    hailo_analytics::pipeline::AppStatus process(hailo_analytics::pipeline::BufferPtr buffer) override;

    // Returns the most recently received JPEG, or std::nullopt if the ring
    // is empty.
    std::optional<std::vector<uint8_t>> latest() const;

    // Returns a copy of all currently cached frames, oldest first.
    std::vector<std::vector<uint8_t>> snapshot_all() const;

    // Drop all cached frames.
    void clear();

    size_t size() const;
    size_t capacity() const
    {
        return m_capacity;
    }

  private:
    size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<std::vector<uint8_t>> m_ring;
};

} // namespace vlm_event_monitor
