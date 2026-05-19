#include "hailo_analytics/pipeline/routing/freeze_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"

namespace hailo_analytics::pipeline::routing
{

FreezeStage::FreezeStage(std::string name, size_t queue_size, bool leaky, bool print_fps)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, print_fps), m_saved_buffer(nullptr),
      m_freeze(false)
{
}

AppStatus FreezeStage::process(BufferPtr data)
{
    if (m_freeze && m_saved_buffer != nullptr)
    {
        data = m_saved_buffer;
    }
    else
    {
        m_saved_buffer = data;
    }

    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

AppStatus FreezeStage::deinit()
{
    m_saved_buffer.reset();
    return AppStatus::SUCCESS;
}
bool FreezeStage::is_freeze()
{
    return m_freeze;
}
void FreezeStage::set_freeze(bool freeze)
{
    m_freeze = freeze;
}
void FreezeStage::set_saved_buffer(BufferPtr buffer)
{
    m_saved_buffer = buffer;
}
BufferPtr FreezeStage::get_saved_buffer()
{
    return m_saved_buffer;
}
void FreezeStage::clear_saved_buffer()
{
    m_saved_buffer = nullptr;
}

FreezeStageBuild::Builder &FreezeStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}
FreezeStageBuild::Builder &FreezeStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}
FreezeStageBuild::Builder &FreezeStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}
FreezeStageBuild::Builder &FreezeStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

std::shared_ptr<FreezeStage> FreezeStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING((m_queue_size != 0), "set_queue_size");

    return std::make_shared<FreezeStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);
}

FreezeStageBuild::Builder FreezeStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::routing
