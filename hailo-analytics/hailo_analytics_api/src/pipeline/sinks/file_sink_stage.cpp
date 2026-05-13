#include "hailo_analytics/pipeline/sinks/file_sink_stage.hpp"
#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::sinks
{

FileSinkStage::FileSinkStage(std::string name, size_t queue_size, bool leaky, bool trace_processing_operations,
                             bool print_fps)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_file(nullptr), m_print_fps(print_fps)
{
}

AppStatus FileSinkStage::create(std::string filepath, EncodingType type, bool /*print_fps*/)
{
    if (m_file == nullptr)
    {
        tl::expected<FileSinkModulePtr, AppStatus> file_expected =
            FileSinkModule::create(m_stage_name, filepath, type, m_print_fps);
        if (!file_expected.has_value())
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to create file module");
            return AppStatus::CONFIGURATION_ERROR;
        }
        m_file = file_expected.value();
        m_filepath = filepath;
        m_type = type;
    }
    return AppStatus::SUCCESS;
}

AppStatus FileSinkStage::init()
{
    if (m_file == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("File {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    m_file->start();
    return AppStatus::SUCCESS;
}

AppStatus FileSinkStage::deinit()
{
    if (m_file)
    {
        m_file->stop();
    }
    return AppStatus::SUCCESS;
}

AppStatus FileSinkStage::configure(std::string filepath, EncodingType type)
{
    if (m_file == nullptr)
    {
        return create(filepath, type, m_print_fps);
    }
    m_file->stop();
    m_file = nullptr;
    return create(filepath, type, m_print_fps);
}

AppStatus FileSinkStage::process(BufferPtr data)
{
    if (m_file == nullptr)
    {
        HAILO_ANALYTICS_LOG_ERROR("File {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    std::vector<hailo_analytics::pipeline::MetadataPtr> metadata =
        data->get_metadata_of_type(hailo_analytics::pipeline::MetadataType::SIZE);
    if (metadata.size() <= 0)
    {
        HAILO_ANALYTICS_LOG_ERROR("File {} got buffer of unknown size, add SizeMeta", m_stage_name);
        return AppStatus::PIPELINE_ERROR;
    }
    hailo_analytics::pipeline::SizeMetadataPtr size_metadata =
        std::dynamic_pointer_cast<hailo_analytics::pipeline::SizeMetadata>(metadata[0]);
    size_t size = size_metadata->get_size();
    m_file->add_buffer(data->get_buffer(), size);

    return AppStatus::SUCCESS;
}

FileSinkStageBuild::Builder &FileSinkStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

FileSinkStageBuild::Builder &FileSinkStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

FileSinkStageBuild::Builder &FileSinkStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

FileSinkStageBuild::Builder &FileSinkStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

FileSinkStageBuild::Builder &FileSinkStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<FileSinkStage> FileSinkStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<FileSinkStage>(m_stage_name.value(), m_queue_size, m_leaky, m_trace, m_print_fps);
}

FileSinkStageBuild::Builder FileSinkStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::sinks
