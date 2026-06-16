// General includes
#include <stddef.h>
#include <tl/expected.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Infra includes
#include "hailo_analytics/pipeline/sinks/udp_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/sinks/output_module.hpp"
#include "hailo_analytics/pipeline/sinks/udp_module.hpp"

namespace hailo_analytics::pipeline::sinks
{

UdpStage::UdpStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations, bool print_fps)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_print_fps(print_fps)
{
}

AppStatus UdpStage::create(std::string host, std::string port, EncodingType type)
{
    if (m_udp == nullptr)
    {
        tl::expected<UdpModulePtr, AppStatus> udp_expected =
            UdpModule::create(m_stage_name, host, port, type, m_print_fps);
        if (!udp_expected.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create udp");
            return AppStatus::CONFIGURATION_ERROR;
        }
        m_udp = udp_expected.value();
        m_host = host;
        m_port = port;
        m_type = type;
    }
    return AppStatus::SUCCESS;
}

AppStatus UdpStage::init()
{
    if (m_udp == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("Udp {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    m_udp->start();
    return AppStatus::SUCCESS;
}

AppStatus UdpStage::deinit()
{
    m_udp->stop();
    return AppStatus::SUCCESS;
}

AppStatus UdpStage::configure(std::string host, std::string port, EncodingType type)
{
    if (m_udp == nullptr)
    {
        return create(host, port, type);
    }
    m_udp->stop();
    m_udp = nullptr;
    return create(host, port, type);
}

AppStatus UdpStage::process(BufferPtr data)
{
    if (m_udp == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("Udp {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    std::vector<hailo_analytics::pipeline::MetadataPtr> metadata =
        data->get_metadata_of_type(hailo_analytics::pipeline::MetadataType::SIZE);
    if (metadata.size() <= 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("Udp {} got buffer of unknown size, add SizeMeta", m_stage_name);
        return AppStatus::PIPELINE_ERROR;
    }
    hailo_analytics::pipeline::SizeMetadataPtr size_metadata =
        std::dynamic_pointer_cast<hailo_analytics::pipeline::SizeMetadata>(metadata[0]);
    size_t size = size_metadata->get_size();
    m_udp->add_buffer(data->get_buffer(), size);

    return AppStatus::SUCCESS;
}

UdpStageBuild::Builder &UdpStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
UdpStageBuild::Builder &UdpStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}
UdpStageBuild::Builder &UdpStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
UdpStageBuild::Builder &UdpStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}
UdpStageBuild::Builder &UdpStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<UdpStage> UdpStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<UdpStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace, m_print_fps);
}

UdpStageBuild::Builder UdpStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sinks
