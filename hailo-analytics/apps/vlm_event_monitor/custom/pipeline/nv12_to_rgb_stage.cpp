#include "nv12_to_rgb_stage.hpp"

#include <algorithm>
#include <cmath>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_event_monitor
{

namespace
{

inline uint8_t clip_byte(double value)
{
    if (value <= 0.0)
    {
        return 0;
    }
    if (value >= 255.0)
    {
        return 255;
    }
    return static_cast<uint8_t>(value + 0.5);
}

#define Y2R(Y, U, V) clip_byte(1.164 * ((Y) - 16) + 1.596 * ((V) - 128))
#define Y2G(Y, U, V) clip_byte(1.164 * ((Y) - 16) - 0.392 * ((U) - 128) - 0.813 * ((V) - 128))
#define Y2B(Y, U, V) clip_byte(1.164 * ((Y) - 16) + 2.017 * ((U) - 128))
} // namespace

using hailo_analytics::pipeline::AppStatus;
using hailo_analytics::pipeline::BufferPtr;

Nv12ToRgbStage::Nv12ToRgbStage(std::string name, uint32_t height, uint32_t width, size_t queue_size, bool leaky,
                               bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations),
      m_height(height), m_width(width)
{
}

AppStatus Nv12ToRgbStage::init()
{
    HAILO_ANALYTICS_LOG_INFO("{}: configured for {}x{} NV12 -> RGB", m_stage_name, m_width, m_height);
    return AppStatus::SUCCESS;
}

AppStatus Nv12ToRgbStage::deinit()
{
    HAILO_ANALYTICS_LOG_INFO("{}: deinit; processed {} frames", m_stage_name, m_frames_processed.load());
    return AppStatus::SUCCESS;
}

void Nv12ToRgbStage::set_callback(RgbCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    m_callback = std::move(callback);
}

AppStatus Nv12ToRgbStage::process(BufferPtr buffer)
{
    if (!buffer || !buffer->get_buffer())
    {
        return AppStatus::SUCCESS;
    }

    auto media_buffer = buffer->get_buffer();
    if (media_buffer->get_num_of_planes() < 2)
    {
        HAILO_ANALYTICS_LOG_WARN("{}: NV12 expects 2 planes, got {}", m_stage_name, media_buffer->get_num_of_planes());
        return AppStatus::SUCCESS;
    }

    const auto *y_plane = static_cast<const uint8_t *>(media_buffer->get_plane_ptr(0));
    const auto *uv_plane = static_cast<const uint8_t *>(media_buffer->get_plane_ptr(1));
    if (y_plane == nullptr || uv_plane == nullptr)
    {
        HAILO_ANALYTICS_LOG_WARN("{}: NV12 plane pointer is null", m_stage_name);
        return AppStatus::SUCCESS;
    }

    const uint32_t y_stride = media_buffer->get_plane_stride(0);
    const uint32_t uv_stride = media_buffer->get_plane_stride(1);

    std::vector<uint8_t> rgb(output_bytes());
    convert_nv12_to_rgb(y_plane, y_stride, uv_plane, uv_stride, rgb.data());

    RgbCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        callback = m_callback;
    }
    if (callback)
    {
        callback(std::move(rgb));
    }

    m_frames_processed.fetch_add(1, std::memory_order_relaxed);
    return AppStatus::SUCCESS;
}

void Nv12ToRgbStage::convert_nv12_to_rgb(const uint8_t *y_plane, uint32_t y_stride, const uint8_t *uv_plane,
                                         uint32_t uv_stride, uint8_t *rgb_out) const
{
    // NV12 layout:
    //   Y  plane: H rows of `y_stride` bytes (W actual, padded to stride)
    //   UV plane: H/2 rows of `uv_stride` bytes — interleaved [U0 V0 U1 V1 …]
    //             each (U,V) pair shared across a 2x2 Y block.
    for (uint32_t row = 0; row < m_height; row++)
    {
        const uint8_t *y_row = y_plane + (static_cast<size_t>(row) * y_stride);
        const uint8_t *uv_row = uv_plane + (static_cast<size_t>(row / 2) * uv_stride);
        uint8_t *rgb_row = rgb_out + (static_cast<size_t>(row) * m_width * kRgbChannels);

        for (uint32_t col = 0; col < m_width; col++)
        {
            const int y_value = y_row[col];
            const int u_value = uv_row[(col / 2) * 2];
            const int v_value = uv_row[(col / 2) * 2 + 1];

            rgb_row[col * kRgbChannels + 0] = Y2R(y_value, u_value, v_value);
            rgb_row[col * kRgbChannels + 1] = Y2G(y_value, u_value, v_value);
            rgb_row[col * kRgbChannels + 2] = Y2B(y_value, u_value, v_value);
        }
    }
}

} // namespace vlm_event_monitor
