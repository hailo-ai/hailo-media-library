#include "jpeg_ring_buffer_stage.hpp"

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_event_monitor
{

using hailo_analytics::pipeline::AppStatus;
using hailo_analytics::pipeline::BufferPtr;
using hailo_analytics::pipeline::MetadataPtr;
using hailo_analytics::pipeline::MetadataType;
using hailo_analytics::pipeline::SizeMetadata;
using hailo_analytics::pipeline::SizeMetadataPtr;

JpegRingBufferStage::JpegRingBufferStage(std::string name, size_t capacity, size_t queue_size, bool leaky,
                                         bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(std::move(name), queue_size, leaky, trace_processing_operations),
      m_capacity(capacity)
{
}

AppStatus JpegRingBufferStage::init()
{
    return AppStatus::SUCCESS;
}

AppStatus JpegRingBufferStage::deinit()
{
    clear();
    return AppStatus::SUCCESS;
}

AppStatus JpegRingBufferStage::process(BufferPtr buffer)
{
    if (!buffer || !buffer->get_buffer())
    {
        HAILO_ANALYTICS_LOG_WARN("{}: dropped null buffer", m_stage_name);
        return AppStatus::SUCCESS;
    }

    // SizeMetadata is added by EncoderStage and carries the encoded byte count.
    auto metadata_list = buffer->get_metadata_of_type(MetadataType::SIZE);
    if (metadata_list.empty())
    {
        HAILO_ANALYTICS_LOG_WARN("{}: encoder buffer missing SizeMetadata; dropping", m_stage_name);
        return AppStatus::SUCCESS;
    }

    auto size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata_list[0]);
    if (!size_metadata)
    {
        return AppStatus::SUCCESS;
    }

    const size_t encoded_size = size_metadata->get_size();
    const auto *jpeg_data = static_cast<const uint8_t *>(buffer->get_buffer()->get_plane_ptr(0));
    if (jpeg_data == nullptr || encoded_size == 0)
    {
        HAILO_ANALYTICS_LOG_WARN("{}: encoded buffer has no payload (size={})", m_stage_name, encoded_size);
        return AppStatus::SUCCESS;
    }

    std::vector<uint8_t> copy(jpeg_data, jpeg_data + encoded_size);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_ring.size() >= m_capacity)
        {
            m_ring.pop_front();
        }
        m_ring.push_back(std::move(copy));
    }
    return AppStatus::SUCCESS;
}

std::optional<std::vector<uint8_t>> JpegRingBufferStage::latest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ring.empty())
    {
        return std::nullopt;
    }
    return m_ring.back();
}

std::vector<std::vector<uint8_t>> JpegRingBufferStage::snapshot_all() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<std::vector<uint8_t>>(m_ring.begin(), m_ring.end());
}

void JpegRingBufferStage::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ring.clear();
}

size_t JpegRingBufferStage::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ring.size();
}

} // namespace vlm_event_monitor
