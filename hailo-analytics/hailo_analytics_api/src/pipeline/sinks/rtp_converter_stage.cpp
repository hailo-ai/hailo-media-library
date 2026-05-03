// General includes
#include <algorithm>

// Media-Library includes
#include "media_library/encoder.hpp"

// Infra includes
#include "hailo_analytics/pipeline/sinks/rtp_converter_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::sinks
{

using RTPReceiver = RTPConverterStage::RTPReceiver;

RTPConverterStage::RTPConverterStage(std::string name, std::shared_ptr<RTPReceiver> receiver, size_t queue_size,
                                     bool leaky, bool trace_processing_operations, std::string session_name)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_rtp_converter(nullptr),
      m_receiver(receiver), m_running(false), m_session_name(session_name)
{
}

AppStatus RTPConverterStage::create(EncodingType type)
{
    if (m_rtp_converter == nullptr)
    {
        auto rtp_converter_expected = ConvertRtpModule::create(m_stage_name, type, false);
        if (!rtp_converter_expected.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create RTP converter for stage {}", m_stage_name);
            return AppStatus::CONFIGURATION_ERROR;
        }
        m_rtp_converter = rtp_converter_expected.value();
        m_encoding_type = type;
    }
    return AppStatus::SUCCESS;
}

AppStatus RTPConverterStage::init()
{
    if (m_rtp_converter == nullptr)
    {
        auto ret = create(m_encoding_type);
        if (ret != AppStatus::SUCCESS)
        {
            HAILO_ANALYTICS_LOG_ERROR("RTP Converter {} initialization failed", m_stage_name);
            return ret;
        }
    }
    m_rtp_converter->start();
    m_session_id = m_receiver->start(m_session_name);
    m_running.store(true);
    m_send_thread = std::thread(&RTPConverterStage::callback_worker, this);

    return AppStatus::SUCCESS;
}

AppStatus RTPConverterStage::deinit()
{
    m_running.store(false);
    if (m_send_thread.joinable())
    {
        m_send_thread.join();
    }
    if (m_rtp_converter != nullptr)
    {
        m_rtp_converter->stop();
    }
    m_receiver->stop(m_session_name);
    return AppStatus::SUCCESS;
}

AppStatus RTPConverterStage::configure(EncodingType type)
{
    deinit();
    m_rtp_converter = nullptr;
    return create(type);
}

AppStatus RTPConverterStage::process(BufferPtr data)
{
    if (m_rtp_converter == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("rtp converter {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    std::vector<hailo_analytics::pipeline::MetadataPtr> metadata =
        data->get_metadata_of_type(hailo_analytics::pipeline::MetadataType::SIZE);
    if (metadata.empty())
    {
        HAILO_ANALYTICS_LOG_ERROR("rtp converter {} got buffer of unknown size, add SizeMeta", m_stage_name);
        return AppStatus::PIPELINE_ERROR;
    }

    auto size_metadata = std::dynamic_pointer_cast<hailo_analytics::pipeline::SizeMetadata>(metadata[0]);
    size_t size = size_metadata->get_size();

    m_rtp_converter->add_buffer(data->get_buffer(), size);

    return AppStatus::SUCCESS;
}

void RTPConverterStage::callback_worker()
{
    while (m_running.load())
    {
        GstSample *sample = m_rtp_converter->get_frame();
        if (sample == nullptr)
        {
            continue;
        }

        m_receiver->on_rtp_packet(sample, m_session_name); // Use stream_name for broadcasting
        gst_sample_unref(sample);
    }
}

RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_session_name(std::string session_name)
{
    m_session_name = session_name;
    return *this;
}
RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_rtp_receiver(
    std::shared_ptr<RTPConverterStage::RTPReceiver> receiver)
{
    m_receiver = receiver;
    return *this;
}
RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_encoding_type(EncodingType type)
{
    m_encoding_type = type;
    return *this;
}

RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

RTPConverterStageBuild::Builder &RTPConverterStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::unique_ptr<RTPConverterStage> RTPConverterStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_receiver != nullptr, "set_rtp_receiver");
    THROW_IF_MISSING(m_session_name.has_value(), "set_session_name");

    auto stage = std::make_unique<RTPConverterStage>(m_stage_name.value(), m_receiver, m_queue_size, m_leaky, m_trace,
                                                     m_session_name.value());
    stage->configure(m_encoding_type);
    return stage;
}

RTPConverterStageBuild::Builder RTPConverterStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sinks
