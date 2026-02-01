#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/muxing/muxer_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::muxing
{

MuxerStage::MuxerStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
                       std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky,
                       bool trace_processing_operations)
    : hailo_analytics::pipeline::ThreadedStage(name, main_queue_size, main_queue_leaky, trace_processing_operations),
      m_main_inlet_name(main_inlet_name), m_main_queue_size(main_queue_size), m_sub_inlet_name(sub_inlet_name),
      m_sub_queue_size(sub_queue_size)
{
    m_queues.push_back(std::make_shared<Queue>(name, m_main_inlet_name, m_main_queue_size, main_queue_leaky));
    m_queues.push_back(std::make_shared<Queue>(name, m_sub_inlet_name, m_sub_queue_size, sub_queue_leaky));
}

void MuxerStage::add_queue(std::string name)
{
    // Muxer has fixed queues - main and sub
    (void)name;
}

void MuxerStage::loop()
{
    while (!m_end_of_stream)
    {
        // Get main buffer from main queue
        BufferPtr main_buffer = m_queues[0]->pop();
        m_tracing->trace_processing_start();

        if (main_buffer == nullptr)
        {
            // End of stream or flushing
            break;
        }

        // Get sub buffer from sub queue (always blocking)
        BufferPtr sub_buffer = m_queues[1]->pop();

        if (sub_buffer == nullptr)
        {
            // Sub queue is flushing or end of stream - just break
            break;
        }

        // If we have both buffers, add sub buffer as metadata to the main buffer
        BufferMetadataPtr sub_metadata = std::make_shared<BufferMetadata>(sub_buffer);
        main_buffer->add_metadata(sub_metadata);

        // Add timestamp and send the main buffer with sub buffer as metadata
        m_tracing->trace_processing_end();
        send_to_subscribers(main_buffer);
    }
}

// Builder implementation
MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_main_inlet_name(std::string name)
{
    m_main_inlet_name = name;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_main_queue_size(size_t size)
{
    m_main_queue_size = size;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_main_leaky(bool leaky)
{
    m_main_queue_leaky = leaky;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_sub_inlet_name(std::string name)
{
    m_sub_inlet_name = name;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_sub_queue_size(size_t size)
{
    m_sub_queue_size = size;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_sub_leaky(bool leaky)
{
    m_sub_queue_leaky = leaky;
    return *this;
}

MuxerStageBuild::Builder &MuxerStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<MuxerStage> MuxerStageBuild::Builder::buildptr() const
{
    if (!m_stage_name.has_value())
    {
        throw std::invalid_argument("Stage name is required");
    }
    if (!m_main_inlet_name.has_value())
    {
        throw std::invalid_argument("Main inlet name is required");
    }
    if (!m_sub_inlet_name.has_value())
    {
        throw std::invalid_argument("Sub inlet name is required");
    }

    return std::make_shared<MuxerStage>(m_stage_name.value(), m_main_inlet_name.value(), m_main_queue_size,
                                        m_main_queue_leaky, m_sub_inlet_name.value(), m_sub_queue_size,
                                        m_sub_queue_leaky, m_trace);
}

MuxerStageBuild::Builder MuxerStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::muxing
