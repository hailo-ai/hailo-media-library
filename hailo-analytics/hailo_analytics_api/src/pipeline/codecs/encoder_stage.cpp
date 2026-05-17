#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::codecs
{

EncoderStage::EncoderStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
{
}

AppStatus EncoderStage::create(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id)
{
    m_media_library = media_library;
    m_stream_id = stream_id;
    auto status = m_media_library->subscribe_to_encoder_output(
        m_stream_id, [this](HailoMediaLibraryBufferPtr buffer, size_t size) {
            hailo_analytics::pipeline::BufferPtr wrapped_buffer =
                std::make_shared<hailo_analytics::pipeline::Buffer>(buffer);
            SizeMetadataPtr size_meta = std::make_shared<SizeMetadata>(this->m_stage_name, size);
            wrapped_buffer->add_metadata(size_meta);
            this->send_to_subscribers(wrapped_buffer);
        });
    if (status != MEDIA_LIBRARY_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to subscribe to encoder output for stream '{}'", stream_id);
        m_media_library.reset();
        return AppStatus::INVALID_ARGUMENT;
    }
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::init()
{
    if (!m_media_library)
    {
        HAILO_ANALYTICS_LOG_ERROR("Encoder {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    // Pipeline start/stop is handled by FrontendStage — encoder does nothing here
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::deinit()
{
    if (m_media_library)
    {
        m_media_library->unsubscribe_from_encoder_output(m_stream_id);
        m_media_library.reset();
    }
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::configure(MediaLibraryInterfacePtr media_library, const output_stream_id_t &stream_id)
{
    if (m_media_library)
    {
        m_media_library.reset();
    }
    return create(media_library, stream_id);
}

AppStatus EncoderStage::process(hailo_analytics::pipeline::BufferPtr data)
{
    if (!m_media_library)
    {
        HAILO_ANALYTICS_LOG_ERROR("Encoder {} not initialized", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    m_media_library->add_buffer_to_encoder(m_stream_id, data->get_buffer());
    return AppStatus::SUCCESS;
}

EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
EncoderStageBuild::Builder &EncoderStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<EncoderStage> EncoderStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_unique<EncoderStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace);
}

EncoderStageBuild::Builder EncoderStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::codecs
