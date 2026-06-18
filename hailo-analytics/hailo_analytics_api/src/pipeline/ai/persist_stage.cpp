#include "hailo_analytics/pipeline/ai/persist_stage.hpp"

#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <chrono>
#include <iostream>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_postprocess_tools/objects/hailo_common.hpp"

namespace hailo_analytics::pipeline::ai
{

PersistStage::PersistStage(std::string name, size_t expiration, size_t queue_size, bool leaky,
                           bool trace_processing_operations, bool print_fps)
    : ThreadedStage(name, queue_size, leaky, trace_processing_operations), m_expiration_threshold(expiration),
      m_print_fps(print_fps)
{
}

AppStatus PersistStage::process(BufferPtr data)
{
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    HailoROIPtr hailo_roi = data->get_roi();

    std::vector<HailoDetectionPtr> incoming_detections = hailo_common::get_hailo_detections(hailo_roi);
    if (incoming_detections.size() > 0)
    {
        m_detections = incoming_detections;
    }
    else if (m_detections.size() > 0)
    {
        hailo_common::add_detection_pointers(hailo_roi, m_detections);
        ++m_count;
        if (m_count >= m_expiration_threshold)
        {
            m_detections.clear();
            m_count = 0;
        }
    }

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    if (m_print_fps)
    {
        std::cout << "Persist time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()
                  << "[microseconds]" << std::endl;
    }
    HAILO_ANALYTICS_LOG_DEBUG("Persist time = {}[microseconds]",
                              std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

PersistStageBuild::Builder &PersistStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

PersistStageBuild::Builder &PersistStageBuild::Builder::set_expiration_opt(size_t expiration)
{
    m_expiration = expiration;
    return *this;
}

PersistStageBuild::Builder &PersistStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

PersistStageBuild::Builder &PersistStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

PersistStageBuild::Builder &PersistStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

PersistStageBuild::Builder &PersistStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

std::shared_ptr<PersistStage> PersistStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<PersistStage>(m_stage_name.value(), m_expiration, m_queue_size, m_leaky, m_trace,
                                          m_print_fps);
}

PersistStageBuild::Builder PersistStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
