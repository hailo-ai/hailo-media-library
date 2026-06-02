#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace vlm_event_monitor
{

// Pipeline-tail (SINK) stage that consumes 336x336 NV12 buffers from the
// frontend and produces a packed RGB byte buffer (`H * W * C` bytes,
// channel order R,G,B,R,G,B,…).
//
// This stage performs *only* a colour-format conversion. The medialib profile
// produces the 336x336 input via LETTERBOX_MIDDLE on the application_settings
// resolution list, so no scaling/letterboxing is needed in this stage.
//
// Conversion is done in software, mirroring the BT.601 limited-range macro
// style used by `hailomat.hpp` (RGB2Y/U/V) — the inverse coefficients are
// kept private to nv12_to_rgb_stage.cpp. At 336*336 = 112,896 pixels and a
// 1 FPS source rate this is trivial CPU work; NEON optimisation is a free
// upgrade later.
//
// Each emitted RGB buffer is delivered to a single consumer callback set via
// set_callback() — typically the FrameSampler held by the EventCheckRunner.
// If no callback is registered, frames are dropped silently.
class Nv12ToRgbStage : public hailo_analytics::pipeline::ThreadedStage
{
  public:
    using RgbCallback = std::function<void(std::vector<uint8_t> rgb)>;

    Nv12ToRgbStage(std::string name, uint32_t height, uint32_t width, size_t queue_size = 1, bool leaky = true,
                   bool trace_processing_operations = true);

    hailo_analytics::pipeline::AppStatus init() override;
    hailo_analytics::pipeline::AppStatus deinit() override;
    hailo_analytics::pipeline::AppStatus process(hailo_analytics::pipeline::BufferPtr buffer) override;

    // Register the consumer callback. Replacing an existing callback is allowed.
    void set_callback(RgbCallback callback);

    uint32_t height() const
    {
        return m_height;
    }
    uint32_t width() const
    {
        return m_width;
    }
    size_t output_bytes() const
    {
        return static_cast<size_t>(m_height) * m_width * kRgbChannels;
    }

  private:
    static constexpr uint32_t kRgbChannels = 3;

    void convert_nv12_to_rgb(const uint8_t *y_plane, uint32_t y_stride, const uint8_t *uv_plane, uint32_t uv_stride,
                             uint8_t *rgb_out) const;

    uint32_t m_height;
    uint32_t m_width;

    std::mutex m_callback_mutex;
    RgbCallback m_callback;

    std::atomic<uint64_t> m_frames_processed{0};
};

} // namespace vlm_event_monitor
