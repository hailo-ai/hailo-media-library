#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/codecs/encoder_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::codecs
{

EncoderStage::EncoderStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations)
{
    m_encoder = nullptr;
}

AppStatus EncoderStage::create(MediaLibraryEncoder &encoder)
{
    m_encoder = &encoder; // TODO because the encoder not created in the stage i cant control its name
    m_encoder->subscribe([this](HailoMediaLibraryBufferPtr buffer, size_t size) {
        // Keep in mind, this does not pass Buffer metadata from encoder input to the next stage
        // It is generally assumed that this is near the end of pipeline.
        hailo_analytics::pipeline::BufferPtr wrapped_buffer =
            std::make_shared<hailo_analytics::pipeline::Buffer>(buffer);
        SizeMetadataPtr size_meta = std::make_shared<SizeMetadata>(this->m_stage_name, size);
        wrapped_buffer->add_metadata(size_meta);
        this->send_to_subscribers(wrapped_buffer);
    });
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::init()
{
    if (m_encoder == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("Encoder {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    m_encoder->start();
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::deinit()
{
    if (m_encoder == nullptr)
        return AppStatus::SUCCESS;
    m_encoder->stop();
    m_encoder->unsubscribe();
    m_encoder = nullptr;
    return AppStatus::SUCCESS;
}

AppStatus EncoderStage::configure(MediaLibraryEncoder &encoder)
{
    if (m_encoder != nullptr)
    {
        this->deinit();
    }
    return create(encoder);
}

AppStatus EncoderStage::process(hailo_analytics::pipeline::BufferPtr data)
{
    if (m_encoder == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("Encoder {} not initialized", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    m_encoder->add_buffer(data->get_buffer());
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
